#include "steering_trim_calibration.hpp"

#include <algorithm>
#include <cmath>

namespace rc_vehicle {

bool SteeringTrimCalibration::Start(float target_accel_g, float current_trim,
                                    float steer_to_yaw_rate_dps) {
  if (phase_ != Phase::Idle && phase_ != Phase::Done &&
      phase_ != Phase::Failed) {
    return false;  // Уже идёт калибровка
  }

  target_accel_g_ = std::clamp(target_accel_g, 0.02f, 0.3f);
  current_trim_ = current_trim;
  steer_to_yaw_rate_dps_ = std::max(steer_to_yaw_rate_dps, 10.0f);

  // PID для управления газом (аналогично auto_forward)
  PidController::Gains gains;
  gains.kp = 1.0f;
  gains.ki = 0.5f;
  gains.kd = 0.05f;
  gains.max_integral = 0.4f;
  gains.max_output = 0.5f;
  accel_pid_.SetGains(gains);
  accel_pid_.Reset();

  phase_ = Phase::Accelerate;
  phase_elapsed_sec_ = 0.0f;
  cruise_throttle_ = 0.0f;
  yaw_rate_sum_ = 0.0;
  yaw_rate_count_ = 0;
  result_ = Result{};

  return true;
}

void SteeringTrimCalibration::Stop() {
  if (phase_ != Phase::Idle && phase_ != Phase::Done &&
      phase_ != Phase::Failed) {
    phase_ = Phase::Failed;
    result_.valid = false;
  }
  accel_pid_.Reset();
}

void SteeringTrimCalibration::Reset() {
  phase_ = Phase::Idle;
  result_ = Result{};
  accel_pid_.Reset();
  phase_elapsed_sec_ = 0.0f;
  cruise_throttle_ = 0.0f;
  yaw_rate_sum_ = 0.0;
  yaw_rate_count_ = 0;
}

void SteeringTrimCalibration::Update(float current_accel_g,
                                     float accel_magnitude,
                                     float filtered_gz_dps, float dt_sec,
                                     float& throttle, float& steering) {
  throttle = 0.0f;
  steering = 0.0f;

  if (phase_ == Phase::Idle || phase_ == Phase::Done ||
      phase_ == Phase::Failed || dt_sec <= 0.0f) {
    return;
  }

  phase_elapsed_sec_ += dt_sec;

  switch (phase_) {
    case Phase::Accelerate: {
      float error = target_accel_g_ - current_accel_g;
      throttle = accel_pid_.Step(error, dt_sec);
      throttle = std::clamp(throttle, 0.0f, 0.5f);
      steering = 0.0f;

      if (phase_elapsed_sec_ >= kAccelDurationSec) {
        cruise_throttle_ = throttle;
        phase_ = Phase::Cruise;
        phase_elapsed_sec_ = 0.0f;
        // Сбросить аккумуляторы — первые семплы круиза могут быть шумными
        yaw_rate_sum_ = 0.0;
        yaw_rate_count_ = 0;
      }
      break;
    }

    case Phase::Cruise: {
      throttle = cruise_throttle_;
      steering = 0.0f;

      // Собираем yaw_rate (пропускаем первые 0.5 сек для стабилизации)
      if (phase_elapsed_sec_ > 0.5f) {
        yaw_rate_sum_ += static_cast<double>(filtered_gz_dps);
        yaw_rate_count_++;
      }

      if (phase_elapsed_sec_ >= kCruiseDurationSec) {
        phase_ = Phase::Brake;
        phase_elapsed_sec_ = 0.0f;
        accel_pid_.Reset();
      }
      break;
    }

    case Phase::Brake: {
      throttle = 0.0f;
      steering = 0.0f;

      bool stopped = (std::abs(accel_magnitude - 1.0f) < kStopAccelThresh) &&
                     (std::abs(filtered_gz_dps) < kStopGyroThresh);
      bool timeout = phase_elapsed_sec_ >= kBrakeTimeoutSec;

      if (stopped || timeout) {
        ComputeResult();
      }
      break;
    }

    default:
      break;
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
