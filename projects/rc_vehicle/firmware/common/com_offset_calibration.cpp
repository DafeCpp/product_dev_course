#include "com_offset_calibration.hpp"

#include <algorithm>
#include <cmath>

namespace rc_vehicle {

bool ComOffsetCalibration::Start(float target_accel_g,
                                 float steering_magnitude,
                                 float cruise_duration_sec,
                                 const float* gravity_vec) {
  if (phase_ != Phase::Idle && phase_ != Phase::Done &&
      phase_ != Phase::Failed) {
    return false;
  }

  target_accel_g_ = std::clamp(target_accel_g, 0.02f, 0.3f);
  steering_magnitude_ = std::clamp(steering_magnitude, 0.1f, 1.0f);
  cruise_duration_sec_ = std::clamp(cruise_duration_sec, 3.0f, 30.0f);

  if (gravity_vec) {
    gravity_vec_[0] = gravity_vec[0];
    gravity_vec_[1] = gravity_vec[1];
    gravity_vec_[2] = gravity_vec[2];
  } else {
    gravity_vec_[0] = 0.f;
    gravity_vec_[1] = 0.f;
    gravity_vec_[2] = 1.f;
  }

  PidController::Gains gains;
  gains.kp = 1.0f;
  gains.ki = 0.5f;
  gains.kd = 0.05f;
  gains.max_integral = 0.4f;
  gains.max_output = 0.5f;
  accel_pid_.SetGains(gains);
  accel_pid_.Reset();

  phase_elapsed_sec_ = 0.0f;
  cruise_throttle_ = 0.0f;

  sum_ax_1_ = sum_ay_1_ = sum_gz_1_ = 0.0;
  count_1_ = 0;
  sum_ax_2_ = sum_ay_2_ = sum_gz_2_ = 0.0;
  count_2_ = 0;
  result_ = Result{};

  TransitionTo(Phase::Pass1_Accelerate);
  return true;
}

void ComOffsetCalibration::Stop() {
  if (IsActive()) {
    phase_ = Phase::Failed;
    result_.valid = false;
  }
  accel_pid_.Reset();
}

void ComOffsetCalibration::Reset() {
  phase_ = Phase::Idle;
  accel_pid_.Reset();
  phase_elapsed_sec_ = 0.0f;
  cruise_throttle_ = 0.0f;
  sum_ax_1_ = sum_ay_1_ = sum_gz_1_ = 0.0;
  count_1_ = 0;
  sum_ax_2_ = sum_ay_2_ = sum_gz_2_ = 0.0;
  count_2_ = 0;
  result_ = Result{};
}

void ComOffsetCalibration::Update(float current_accel_g, float accel_magnitude,
                                  float cal_ax, float cal_ay,
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
    // ─────────────── Pass 1: CW (steering = +magnitude) ───────────────

    case Phase::Pass1_Accelerate: {
      float error = target_accel_g_ - current_accel_g;
      throttle = accel_pid_.Step(error, dt_sec);
      throttle = std::clamp(throttle, 0.0f, 0.5f);
      steering = steering_magnitude_;

      if (phase_elapsed_sec_ >= kAccelDurationSec) {
        cruise_throttle_ = throttle;
        TransitionTo(Phase::Pass1_Cruise);
      }
      break;
    }

    case Phase::Pass1_Cruise: {
      throttle = cruise_throttle_;
      steering = steering_magnitude_;

      if (phase_elapsed_sec_ > kSettleSkipSec) {
        sum_ax_1_ += static_cast<double>(cal_ax);
        sum_ay_1_ += static_cast<double>(cal_ay);
        sum_gz_1_ += static_cast<double>(filtered_gz_dps);
        count_1_++;
      }

      if (phase_elapsed_sec_ >= cruise_duration_sec_) {
        TransitionTo(Phase::Pass1_Brake);
      }
      break;
    }

    case Phase::Pass1_Brake: {
      throttle = 0.0f;
      steering = 0.0f;

      bool stopped = (std::abs(accel_magnitude - 1.0f) < kStopAccelThresh) &&
                     (std::abs(filtered_gz_dps) < kStopGyroThresh);
      bool timeout = phase_elapsed_sec_ >= kBrakeTimeoutSec;

      if (stopped || timeout) {
        // Перезапустить PID для второго прохода
        accel_pid_.Reset();
        TransitionTo(Phase::Pass2_Accelerate);
      }
      break;
    }

    // ─────────────── Pass 2: CCW (steering = -magnitude) ──────────────

    case Phase::Pass2_Accelerate: {
      float error = target_accel_g_ - current_accel_g;
      throttle = accel_pid_.Step(error, dt_sec);
      throttle = std::clamp(throttle, 0.0f, 0.5f);
      steering = -steering_magnitude_;

      if (phase_elapsed_sec_ >= kAccelDurationSec) {
        cruise_throttle_ = throttle;
        TransitionTo(Phase::Pass2_Cruise);
      }
      break;
    }

    case Phase::Pass2_Cruise: {
      throttle = cruise_throttle_;
      steering = -steering_magnitude_;

      if (phase_elapsed_sec_ > kSettleSkipSec) {
        sum_ax_2_ += static_cast<double>(cal_ax);
        sum_ay_2_ += static_cast<double>(cal_ay);
        sum_gz_2_ += static_cast<double>(filtered_gz_dps);
        count_2_++;
      }

      if (phase_elapsed_sec_ >= cruise_duration_sec_) {
        TransitionTo(Phase::Pass2_Brake);
      }
      break;
    }

    case Phase::Pass2_Brake: {
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

void ComOffsetCalibration::TransitionTo(Phase next) {
  phase_ = next;
  phase_elapsed_sec_ = 0.0f;
  if (next == Phase::Pass1_Brake || next == Phase::Pass2_Brake) {
    accel_pid_.Reset();
  }
}

void ComOffsetCalibration::ComputeResult() {
  result_.samples_cw = count_1_;
  result_.samples_ccw = count_2_;

  if (count_1_ < kMinSamples || count_2_ < kMinSamples) {
    phase_ = Phase::Failed;
    result_.valid = false;
    return;
  }

  // Средние значения
  const float mean_ax_1 = static_cast<float>(sum_ax_1_ / count_1_);
  const float mean_ay_1 = static_cast<float>(sum_ay_1_ / count_1_);
  const float mean_gz_1 = static_cast<float>(sum_gz_1_ / count_1_);

  const float mean_ax_2 = static_cast<float>(sum_ax_2_ / count_2_);
  const float mean_ay_2 = static_cast<float>(sum_ay_2_ / count_2_);
  const float mean_gz_2 = static_cast<float>(sum_gz_2_ / count_2_);

  result_.omega_cw_dps = mean_gz_1;
  result_.omega_ccw_dps = mean_gz_2;

  // Проверка: достаточная угловая скорость
  if (std::abs(mean_gz_1) < kMinOmegaDps ||
      std::abs(mean_gz_2) < kMinOmegaDps) {
    phase_ = Phase::Failed;
    result_.valid = false;
    return;
  }

  // Проверка: повороты в разные стороны
  if (mean_gz_1 * mean_gz_2 > 0.0f) {
    // Оба поворота в одну сторону — ошибка
    phase_ = Phase::Failed;
    result_.valid = false;
    return;
  }

  // Линейные ускорения (вычитаем гравитацию)
  const float lin_ax_1 = mean_ax_1 - gravity_vec_[0];
  const float lin_ay_1 = mean_ay_1 - gravity_vec_[1];
  const float lin_ax_2 = mean_ax_2 - gravity_vec_[0];
  const float lin_ay_2 = mean_ay_2 - gravity_vec_[1];

  // ω в рад/с
  constexpr float kDegToRad = 3.14159265358979f / 180.0f;
  const float omega_1_rad = mean_gz_1 * kDegToRad;
  const float omega_2_rad = mean_gz_2 * kDegToRad;
  const float omega_sq_sum = omega_1_rad * omega_1_rad + omega_2_rad * omega_2_rad;

  if (omega_sq_sum < 1e-6f) {
    phase_ = Phase::Failed;
    result_.valid = false;
    return;
  }

  // rx = -(sum_lin_ax) * g / omega_sq_sum
  // ry = -(sum_lin_ay) * g / omega_sq_sum
  const float rx = -(lin_ax_1 + lin_ax_2) * kGravity / omega_sq_sum;
  const float ry = -(lin_ay_1 + lin_ay_2) * kGravity / omega_sq_sum;

  // Валидация: offset не слишком большой
  if (std::abs(rx) > kMaxOffsetM || std::abs(ry) > kMaxOffsetM) {
    phase_ = Phase::Failed;
    result_.rx = rx;
    result_.ry = ry;
    result_.valid = false;
    return;
  }

  result_.rx = rx;
  result_.ry = ry;
  result_.valid = true;
  phase_ = Phase::Done;
}

}  // namespace rc_vehicle
