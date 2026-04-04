#include "steering_trim_calibration.hpp"

#include <algorithm>
#include <cmath>

namespace rc_vehicle {

bool SteeringTrimCalibration::Start(float target_accel_g, float current_trim,
                                    float steer_to_yaw_rate_dps) {
  if (IsActive()) {
    return false;  // Уже идёт калибровка
  }

  current_trim_ = current_trim;
  steer_to_yaw_rate_dps_ = std::max(steer_to_yaw_rate_dps, 10.0f);

  float clamped_accel = std::clamp(target_accel_g, 0.02f, 0.3f);

  MotionDriver::Config cfg;
  cfg.accel_mode = MotionDriver::AccelMode::Pid;
  cfg.pid_gains = {0.3f, 0.2f, 0.0f, 0.15f, 0.5f};
  cfg.target_value = clamped_accel;
  cfg.accel_duration_sec = 1.5f;
  cfg.min_effective_throttle = 0.0f;
  cfg.brake_throttle = 0.0f;
  cfg.brake_timeout_sec = 3.0f;
  cfg.zupt = {0.05f, 3.0f};
  cfg.breakaway = {0.5f, 0.25f, 0.03f, 25};

  driver_.Start(cfg);
  phase_ = Phase::Accelerate;
  yaw_rate_sum_ = 0.0;
  yaw_rate_count_ = 0;
  result_ = Result{};

  return true;
}

void SteeringTrimCalibration::Stop() {
  if (IsActive()) {
    phase_ = Phase::Failed;
    result_.valid = false;
  }
  driver_.Reset();
}

void SteeringTrimCalibration::Reset() {
  phase_ = Phase::Idle;
  result_ = Result{};
  driver_.Reset();
  yaw_rate_sum_ = 0.0;
  yaw_rate_count_ = 0;
}

void SteeringTrimCalibration::Update(float current_accel_g,
                                     float accel_magnitude,
                                     float filtered_gz_dps, float dt_sec,
                                     float& throttle, float& steering) {
  throttle = 0.0f;
  steering = 0.0f;

  if (!IsActive() || dt_sec <= 0.0f) {
    return;
  }

  throttle = driver_.Update(current_accel_g, accel_magnitude, filtered_gz_dps,
                            dt_sec);
  steering = 0.0f;

  MotionPhase dp = driver_.GetPhase();

  if (phase_ == Phase::Accelerate) {
    if (dp == MotionPhase::Cruise) {
      phase_ = Phase::Cruise;
      yaw_rate_sum_ = 0.0;
      yaw_rate_count_ = 0;
    }
  }

  if (phase_ == Phase::Cruise) {
    float elapsed = driver_.GetPhaseElapsed();

    // Пропускаем первые kSettleSkipSec для стабилизации
    if (elapsed > kSettleSkipSec) {
      yaw_rate_sum_ += static_cast<double>(filtered_gz_dps);
      yaw_rate_count_++;
    }

    if (elapsed >= kCruiseDurationSec) {
      driver_.EndCruise();
      phase_ = Phase::Brake;
    }
  }

  if (phase_ == Phase::Brake) {
    if (dp == MotionPhase::Stopped) {
      ComputeResult();
    }
  }
}

void SteeringTrimCalibration::ComputeResult() {
  if (yaw_rate_count_ < kMinSamples) {
    phase_ = Phase::Failed;
    result_.valid = false;
    result_.samples = yaw_rate_count_;
    return;
  }

  float mean_yaw_rate =
      static_cast<float>(yaw_rate_sum_ / yaw_rate_count_);

  if (std::abs(mean_yaw_rate) > kMaxYawRateDps) {
    // Слишком большой drift — вероятно проблема с механикой, trim не поможет
    phase_ = Phase::Failed;
    result_.mean_yaw_rate = mean_yaw_rate;
    result_.samples = yaw_rate_count_;
    result_.valid = false;
    return;
  }

  // trim_correction = -mean_yaw_rate / sensitivity
  float trim_correction = -mean_yaw_rate / steer_to_yaw_rate_dps_;
  float new_trim = current_trim_ + trim_correction;

  // Ограничить допустимым диапазоном trim
  new_trim = std::clamp(new_trim, -0.1f, 0.1f);

  result_.trim = new_trim;
  result_.mean_yaw_rate = mean_yaw_rate;
  result_.samples = yaw_rate_count_;
  result_.valid = true;
  phase_ = Phase::Done;
}

}  // namespace rc_vehicle
