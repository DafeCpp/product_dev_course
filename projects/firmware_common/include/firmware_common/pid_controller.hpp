#pragma once

#include <algorithm>

namespace firmware_common {

/** Discrete PID controller with anti-windup and output limits. */
class PidController {
 public:
  struct Gains {
    float kp{0.0f};
    float ki{0.0f};
    float kd{0.0f};
    float max_integral{1.0f};
    float max_output{1.0f};
  };

  PidController() = default;
  explicit PidController(const Gains& gains) : gains_(gains) {}

  void SetGains(const Gains& gains) noexcept { gains_ = gains; }
  [[nodiscard]] const Gains& GetGains() const noexcept { return gains_; }

  [[nodiscard]] float Step(float error, float dt_sec) noexcept;
  void Reset() noexcept;

  // Preload the integral term when switching controllers without a step.
  void SetIntegral(float integral) noexcept {
    integral_ = std::clamp(integral, -gains_.max_integral,
                           gains_.max_integral);
  }
  [[nodiscard]] float GetIntegral() const noexcept { return integral_; }

 private:
  Gains gains_{};
  float integral_{0.0f};
  float prev_error_{0.0f};
  bool first_step_{true};
};

}  // namespace firmware_common
