#include "firmware_common/pid_controller.hpp"

namespace firmware_common {

float PidController::Step(float error, float dt_sec) noexcept {
  if (dt_sec <= 0.0f) {
    return 0.0f;
  }

  integral_ += error * dt_sec;
  integral_ = std::clamp(integral_, -gains_.max_integral, gains_.max_integral);

  float derivative = 0.0f;
  if (!first_step_) {
    derivative = (error - prev_error_) / dt_sec;
  }
  first_step_ = false;
  prev_error_ = error;

  const float output =
      gains_.kp * error + gains_.ki * integral_ + gains_.kd * derivative;
  return std::clamp(output, -gains_.max_output, gains_.max_output);
}

void PidController::Reset() noexcept {
  integral_ = 0.0f;
  prev_error_ = 0.0f;
  first_step_ = true;
}

}  // namespace firmware_common
