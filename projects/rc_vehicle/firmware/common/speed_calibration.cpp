#include "speed_calibration.hpp"

#include <algorithm>
#include <cmath>

namespace rc_vehicle {

bool SpeedCalibration::Start(float target_throttle, float cruise_duration_sec) {
  if (IsActive()) return false;
  if (target_throttle < 0.1f || target_throttle > 0.8f) return false;
  if (cruise_duration_sec < 1.0f || cruise_duration_sec > 10.0f) return false;

  target_throttle_ = target_throttle;
  cruise_duration_sec_ = cruise_duration_sec;
  Reset();
  phase_ = Phase::Accelerate;
  return true;
}

void SpeedCalibration::Stop() {
  phase_ = Phase::Idle;
  Reset();
}

void SpeedCalibration::Update(float speed_ms, float accel_mag, float dt_sec,
                              float& throttle, float& steering) {
  steering = 0.0f;
  phase_elapsed_sec_ += dt_sec;

  switch (phase_) {
    // ─────────────────────────────────────────────────────────────────────
    case Phase::Accelerate: {
      // Линейный рост throttle 0 → target_throttle за kAccelSec
      const float ramp = std::min(phase_elapsed_sec_ / kAccelSec, 1.0f);
      throttle = ramp * target_throttle_;
      if (throttle > 0.0f && throttle < kMinEffectiveThrottle) {
        throttle = kMinEffectiveThrottle;
      }
      if (phase_elapsed_sec_ >= kAccelSec) {
        cruise_throttle_ = target_throttle_;
        phase_elapsed_sec_ = 0.0f;
        phase_ = Phase::Cruise;
      }
      break;
    }

    // ─────────────────────────────────────────────────────────────────────
    case Phase::Cruise: {
      throttle = cruise_throttle_;
      // Сбор семплов скорости
      speed_sum_ += static_cast<double>(speed_ms);
      ++speed_count_;

      if (phase_elapsed_sec_ >= cruise_duration_sec_) {
        if (speed_count_ < kMinSamples) {
          phase_ = Phase::Failed;
        } else {
          phase_elapsed_sec_ = 0.0f;
          phase_ = Phase::Brake;
        }
      }
      break;
    }

    // ─────────────────────────────────────────────────────────────────────
    case Phase::Brake: {
      throttle = kBrakeThrottle;

      // Остановка по ZUPT: малое ускорение → машина стоит
      if (accel_mag < kStopAccelThresh) {
        ComputeResult();
        phase_ = Phase::Done;
        throttle = 0.0f;
        break;
      }

      // Таймаут торможения
      if (phase_elapsed_sec_ >= kBrakeTimeoutSec) {
        ComputeResult();
        phase_ = Phase::Done;
        throttle = 0.0f;
      }
      break;
    }

    case Phase::Done:
    case Phase::Failed:
    case Phase::Idle:
      throttle = 0.0f;
      break;
  }
}

void SpeedCalibration::Reset() {
  result_ = {};
  phase_elapsed_sec_ = 0.0f;
  cruise_throttle_ = 0.0f;
  speed_sum_ = 0.0;
  speed_count_ = 0;
}

void SpeedCalibration::ComputeResult() {
  result_.target_throttle = target_throttle_;
  result_.samples = speed_count_;

  if (speed_count_ < kMinSamples || target_throttle_ < 1e-4f) {
    result_.valid = false;
    return;
  }

  result_.mean_speed_ms =
      static_cast<float>(speed_sum_ / static_cast<double>(speed_count_));
  result_.speed_gain = result_.mean_speed_ms / target_throttle_;
  result_.valid = result_.mean_speed_ms > 0.01f;
}

}  // namespace rc_vehicle
