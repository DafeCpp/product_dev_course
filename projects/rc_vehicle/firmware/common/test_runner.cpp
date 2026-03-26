#include "test_runner.hpp"

#include <algorithm>
#include <cmath>

namespace rc_vehicle {

bool TestRunner::Start(const TestParams& params) {
  if (phase_ != Phase::Idle && phase_ != Phase::Done &&
      phase_ != Phase::Failed) {
    return false;
  }

  params_ = params;
  params_.target_accel_g = std::clamp(params_.target_accel_g, 0.02f, 0.3f);
  params_.duration_sec = std::clamp(params_.duration_sec, 1.0f, 30.0f);
  params_.steering = std::clamp(params_.steering, -1.0f, 1.0f);

  PidController::Gains gains;
  gains.kp = 1.0f;
  gains.ki = 0.5f;
  gains.kd = 0.05f;
  gains.max_integral = 0.4f;
  gains.max_output = 0.5f;
  accel_pid_.SetGains(gains);
  accel_pid_.Reset();

  type_ = params_.type;
  total_elapsed_sec_ = 0.0f;
  phase_elapsed_sec_ = 0.0f;
  cruise_throttle_ = 0.0f;

  TransitionTo(Phase::Accelerate);
  return true;
}

void TestRunner::Stop() {
  if (phase_ != Phase::Idle && phase_ != Phase::Done &&
      phase_ != Phase::Failed) {
    TransitionTo(Phase::Failed);
  }
  accel_pid_.Reset();
}

void TestRunner::Reset() {
  phase_ = Phase::Idle;
  type_ = TestType::Straight;
  params_ = TestParams{};
  accel_pid_.Reset();
  total_elapsed_sec_ = 0.0f;
  phase_elapsed_sec_ = 0.0f;
  cruise_throttle_ = 0.0f;
}

TestRunner::Status TestRunner::GetStatus() const {
  Status s;
  s.phase = phase_;
  s.type = type_;
  s.elapsed_sec = total_elapsed_sec_;
  s.phase_elapsed_sec = phase_elapsed_sec_;
  s.valid = (phase_ == Phase::Done);
  return s;
}


void TestRunner::Update(float current_accel_g, float accel_magnitude,
                        float filtered_gz_dps, float dt_sec, float& throttle,
                        float& steering) {
  throttle = 0.0f;
  steering = 0.0f;

  if (phase_ == Phase::Idle || phase_ == Phase::Done ||
      phase_ == Phase::Failed || dt_sec <= 0.0f) {
    return;
  }

  total_elapsed_sec_ += dt_sec;
  phase_elapsed_sec_ += dt_sec;

  switch (phase_) {
    case Phase::Accelerate: {
      float error = params_.target_accel_g - current_accel_g;
      throttle = accel_pid_.Step(error, dt_sec);
      throttle = std::clamp(throttle, 0.0f, 0.5f);
      steering = 0.0f;

      if (phase_elapsed_sec_ >= kAccelDurationSec) {
        cruise_throttle_ = throttle;
        TransitionTo(Phase::Cruise);
      }
      break;
    }

    case Phase::Cruise: {
      throttle = cruise_throttle_;

      switch (type_) {
        case TestType::Straight:
          steering = 0.0f;
          if (phase_elapsed_sec_ >= params_.duration_sec) {
            TransitionTo(Phase::Brake);
          }
          break;

        case TestType::Circle:
          steering = params_.steering;
          if (phase_elapsed_sec_ >= params_.duration_sec) {
            TransitionTo(Phase::Brake);
          }
          break;

        case TestType::Step:
          steering = 0.0f;
          if (phase_elapsed_sec_ >= kStepSettleSec) {
            TransitionTo(Phase::StepExec);
          }
          break;
      }
      break;
    }

    case Phase::StepExec: {
      throttle = cruise_throttle_;
      steering = params_.steering;

      if (phase_elapsed_sec_ >= params_.duration_sec) {
        TransitionTo(Phase::Brake);
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
        TransitionTo(Phase::Done);
      }
      break;
    }

    default:
      break;
  }
}

void TestRunner::TransitionTo(Phase next) {
  phase_ = next;
  phase_elapsed_sec_ = 0.0f;
  if (next == Phase::Brake) {
    accel_pid_.Reset();
  }
}

}  // namespace rc_vehicle
