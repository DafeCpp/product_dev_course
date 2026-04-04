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

  MotionDriver::Config cfg;
  cfg.accel_mode = MotionDriver::AccelMode::Pid;
  cfg.pid_gains = {0.3f, 0.2f, 0.0f, 0.15f, 0.5f};
  cfg.target_value = params_.target_accel_g;
  cfg.accel_duration_sec = 1.5f;
  cfg.min_effective_throttle = 0.0f;
  cfg.brake_throttle = 0.0f;
  cfg.brake_timeout_sec = 3.0f;
  cfg.zupt = {0.05f, 3.0f};
  cfg.breakaway = {0.5f, 0.25f, 0.03f, 25};
  driver_.Start(cfg);

  type_ = params_.type;
  total_elapsed_sec_ = 0.0f;

  TransitionTo(Phase::Accelerate);
  return true;
}

void TestRunner::Stop() {
  if (phase_ != Phase::Idle && phase_ != Phase::Done &&
      phase_ != Phase::Failed) {
    driver_.Reset();
    TransitionTo(Phase::Failed);
  }
}

void TestRunner::Reset() {
  phase_ = Phase::Idle;
  type_ = TestType::Straight;
  params_ = TestParams{};
  driver_.Reset();
  total_elapsed_sec_ = 0.0f;
  phase_elapsed_sec_ = 0.0f;
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

  throttle = driver_.Update(current_accel_g, accel_magnitude, filtered_gz_dps,
                            dt_sec);

  MotionPhase dp = driver_.GetPhase();

  switch (phase_) {
    case Phase::Accelerate: {
      phase_elapsed_sec_ += dt_sec;
      steering = 0.0f;
      if (dp == MotionPhase::Cruise) {
        TransitionTo(Phase::Cruise);
      }
      break;
    }

    case Phase::Cruise: {
      phase_elapsed_sec_ += dt_sec;
      throttle = driver_.GetCruiseThrottle();

      switch (type_) {
        case TestType::Straight:
          steering = 0.0f;
          if (phase_elapsed_sec_ >= params_.duration_sec) {
            driver_.EndCruise();
            TransitionTo(Phase::Brake);
          }
          break;

        case TestType::Circle:
          steering = params_.steering;
          if (phase_elapsed_sec_ >= params_.duration_sec) {
            driver_.EndCruise();
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
      phase_elapsed_sec_ += dt_sec;
      throttle = driver_.GetCruiseThrottle();
      steering = params_.steering;

      if (phase_elapsed_sec_ >= params_.duration_sec) {
        driver_.EndCruise();
        TransitionTo(Phase::Brake);
      }
      break;
    }

    case Phase::Brake: {
      steering = 0.0f;
      if (dp == MotionPhase::Stopped) {
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
}

}  // namespace rc_vehicle
