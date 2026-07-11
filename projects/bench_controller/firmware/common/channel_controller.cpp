#include "channel_controller.hpp"

#include <algorithm>
#include <firmware_common/slew_rate.hpp>

namespace bench {

ChannelController::ChannelController(const Config& config)
    : config_(config),
      force_pid_(config.force_gains),
      disp_pid_(config.disp_gains) {}

void ChannelController::RequestMode(ControlMode mode,
                                    float capture_value) noexcept {
  if (mode == mode_) {
    return;
  }
  mode_ = mode;

  firmware_common::PidController& incoming =
      mode_ == ControlMode::kForce ? force_pid_ : disp_pid_;
  const firmware_common::PidController::Gains& gains = incoming.GetGains();

  incoming.Reset();
  if (gains.ki > 0.0f) {
    // Первый выход при e ≈ 0: u = ki · I  ⇒  I = u_prev / ki.
    incoming.SetIntegral(last_output_ / gains.ki);
    ff_bias_ = 0.0f;
  } else {
    // Без интегратора переносим последнюю команду затухающим смещением.
    ff_bias_ = last_output_;
  }

  capture_active_ = true;
  capture_from_ = capture_value;
  capture_elapsed_s_ = 0.0f;
  effective_target_ = capture_value;
  target_history_ = 0;  // FF молчит, пока не наберётся история цели
}

ValveSetpoint ChannelController::Step(float target, const ValveFeedback& fb,
                                      float dt_sec) noexcept {
  // Рампа захвата: цель движется от захваченного измерения к внешней.
  if (capture_active_) {
    capture_elapsed_s_ += dt_sec;
    if (capture_elapsed_s_ >= config_.capture_ramp_s) {
      capture_active_ = false;
      effective_target_ = target;
    } else {
      const float k = capture_elapsed_s_ / config_.capture_ramp_s;
      effective_target_ = capture_from_ + (target - capture_from_) * k;
    }
  } else {
    effective_target_ = target;
  }

  firmware_common::PidController& active =
      mode_ == ControlMode::kForce ? force_pid_ : disp_pid_;
  const float error = effective_target_ - Measured(fb);

  // Feed-forward по производным цели — без него PI не отслеживает
  // синус 10–50 Гц (полоса контура ограничена лагом золотника);
  // lead-член (τ·a) компенсирует фазовое отставание золотника.
  float u_ff = 0.0f;
  if (target_history_ > 0 && dt_sec > 0.0f) {
    const float ff_gain =
        mode_ == ControlMode::kForce ? config_.force_ff : config_.disp_ff;
    const float velocity =
        (effective_target_ - prev_effective_target_) / dt_sec;
    float lead = 0.0f;
    if (target_history_ > 1 && config_.ff_lead_tau_s > 0.0f) {
      const float accel = (velocity - prev_target_velocity_) / dt_sec;
      lead = config_.ff_lead_tau_s * accel;
    }
    u_ff = ff_gain * (velocity + lead);
    prev_target_velocity_ = velocity;
  }
  prev_effective_target_ = effective_target_;
  if (target_history_ < 2) ++target_history_;

  float u = active.Step(error, dt_sec) + u_ff + ff_bias_;

  // Затухание feed-forward синхронно с рампой захвата.
  if (ff_bias_ != 0.0f && config_.capture_ramp_s > 0.0f) {
    const float decay = dt_sec / config_.capture_ramp_s;
    ff_bias_ = decay >= 1.0f ? 0.0f : ff_bias_ * (1.0f - decay);
  }

  u = std::clamp(u, -1.0f, 1.0f);
  last_output_ = firmware_common::ApplySlewRate(
      u, last_output_, config_.output_slew_per_s, dt_sec);

  return ValveSetpoint{
      .mode = mode_, .value = last_output_, .enable = enabled_};
}

}  // namespace bench
