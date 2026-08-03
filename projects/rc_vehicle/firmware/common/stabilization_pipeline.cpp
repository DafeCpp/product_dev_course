#include "stabilization_pipeline.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace rc_vehicle {

namespace {
// LOS-284: hysteresis prevents EKF noise from repeatedly toggling yaw assist
// near standstill. A separate ramp removes the correction step at engagement.
constexpr float kStabEngageSpeedMs = 0.30f;
constexpr float kStabDisengageSpeedMs = 0.15f;
constexpr float kStabRampInSec = 0.25f;
constexpr float kYawErrorCutoffHz = 10.0f;
constexpr float kTwoPi = 6.28318530717958647692f;
}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// YawRateController
// ─────────────────────────────────────────────────────────────────────────────

void YawRateController::Init(const StabilizationConfig& cfg,
                             const VehicleEkf& ekf, const ImuHandler* imu) {
  assert(imu != nullptr && "YawRateController::Init() requires non-null imu");
  ekf_ = &ekf;
  imu_ = imu;
  SetGains(cfg);
  Reset();
}

void YawRateController::Process(const StabilizationConfig& cfg, float& steering,
                                float stab_w, float mode_w, uint32_t dt_ms,
                                bool reversing) noexcept {
  if (!ekf_ || !imu_) return;
  const StabilizationInput input{
      .dt_ms = dt_ms,
      .speed_ms = ekf_->GetSpeedMs(),
      .filtered_gyro_z_dps = imu_->GetFilteredGyroZ(),
      .stabilization_weight = stab_w,
      .mode_transition_weight = mode_w,
      .imu_enabled = imu_->IsEnabled(),
  };
  Process(cfg, steering, input, reversing);
}

void YawRateController::Process(const StabilizationConfig& cfg, float& steering,
                                const StabilizationInput& input,
                                bool reversing) noexcept {
  if (input.stabilization_weight <= 0.0f || !input.imu_enabled) {
    Reset();
    return;
  }
  if (input.dt_ms == 0) return;

  // FW-R22: в реверсе связь руль→рыскание инвертируется, и yaw-rate обратная
  // связь становится положительной → автоколебания руля (hunting) при том, что
  // руль водителем не трогается. Стабилизацию в реверсе отключаем: руль
  // проходит как есть, PID в сбросе. Направление берём по знаку команды газа,
  // т.к. EKF vx ненадёжен (IMU-only, дрейф: vx_var в логах доходит до 193).
  if (reversing) {
    Reset();
    return;
  }

  const float dt_sec = static_cast<float>(input.dt_ms) * 0.001f;

  if (speed_gate_active_) {
    if (input.speed_ms <= kStabDisengageSpeedMs) {
      Reset();
      return;
    }
  } else {
    if (input.speed_ms < kStabEngageSpeedMs) return;
    speed_gate_active_ = true;
  }

  activation_weight_ =
      std::min(1.0f, activation_weight_ + dt_sec / kStabRampInSec);

  const float omega_desired = cfg.yaw_rate.steer_to_yaw_rate_dps * steering;
  const float omega_actual = input.filtered_gyro_z_dps;
  const float error_dps = omega_desired - omega_actual;
  const float filter_alpha =
      1.0f - std::exp(-kTwoPi * kYawErrorCutoffHz * dt_sec);
  filtered_error_dps_ += filter_alpha * (error_dps - filtered_error_dps_);
  const float pid_out = pid_.Step(filtered_error_dps_, dt_sec);

  // Adaptive PID: масштабирование выхода ПИД по скорости из EKF (Phase 4.1)
  float adaptive_scale = 1.0f;
  if (cfg.adaptive.enabled && cfg.adaptive.speed_ref_ms > 0.0f) {
    adaptive_scale = std::clamp(input.speed_ms / cfg.adaptive.speed_ref_ms,
                                cfg.adaptive.scale_min, cfg.adaptive.scale_max);
  }

  // max_correction is a hard safety limit. Adaptive scaling must not raise the
  // effective ceiling above the value shown in config and telemetry.
  const float correction =
      std::clamp(pid_out * adaptive_scale, -cfg.yaw_rate.pid.max_correction,
                 cfg.yaw_rate.pid.max_correction);
  steering = std::clamp(steering + correction * input.stabilization_weight *
                                       input.mode_transition_weight *
                                       activation_weight_,
                        -1.0f, 1.0f);
}

void YawRateController::SetGains(const StabilizationConfig& cfg) noexcept {
  pid_.SetGains({cfg.yaw_rate.pid.kp, cfg.yaw_rate.pid.ki, cfg.yaw_rate.pid.kd,
                 cfg.yaw_rate.pid.max_integral,
                 cfg.yaw_rate.pid.max_correction});
}

void YawRateController::Reset() noexcept {
  pid_.Reset();
  filtered_error_dps_ = 0.0f;
  activation_weight_ = 0.0f;
  speed_gate_active_ = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// PitchCompensator
// ─────────────────────────────────────────────────────────────────────────────

void PitchCompensator::Init(const MadgwickFilter& madgwick,
                            const ImuHandler* imu) {
  assert(imu != nullptr && "PitchCompensator::Init() requires non-null imu");
  madgwick_ = &madgwick;
  imu_ = imu;
}

void PitchCompensator::Process(const StabilizationConfig& cfg, float& throttle,
                               float stab_w) noexcept {
  if (!madgwick_ || !imu_) return;
  float pitch_deg = 0.0f;
  float roll_deg = 0.0f;
  float yaw_deg = 0.0f;
  madgwick_->GetEulerDeg(pitch_deg, roll_deg, yaw_deg);
  const StabilizationInput input{
      .pitch_deg = pitch_deg,
      .stabilization_weight = stab_w,
      .imu_enabled = imu_->IsEnabled(),
  };
  Process(cfg, throttle, input);
}

void PitchCompensator::Process(const StabilizationConfig& cfg, float& throttle,
                               const StabilizationInput& input) noexcept {
  if (!cfg.pitch_comp.enabled) return;
  if (input.stabilization_weight <= 0.0f) return;
  if (!input.imu_enabled) return;

  // Fix #8 (REFACTORING.md): std::clamp вместо ручного if/else
  const float correction =
      std::clamp(cfg.pitch_comp.gain * input.pitch_deg,
                 -cfg.pitch_comp.max_correction, cfg.pitch_comp.max_correction);

  throttle = std::clamp(throttle + correction * input.stabilization_weight,
                        -1.0f, 1.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// SlipAngleController
// ─────────────────────────────────────────────────────────────────────────────

void SlipAngleController::Init(const StabilizationConfig& cfg,
                               const VehicleEkf& ekf, const ImuHandler* imu) {
  assert(imu != nullptr && "SlipAngleController::Init() requires non-null imu");
  ekf_ = &ekf;
  imu_ = imu;
  SetGains(cfg);
}

void SlipAngleController::Process(const StabilizationConfig& cfg,
                                  float& throttle, float stab_w, float mode_w,
                                  uint32_t dt_ms) noexcept {
  if (!ekf_ || !imu_) return;
  const StabilizationInput input{
      .dt_ms = dt_ms,
      .slip_angle_deg = ekf_->GetSlipAngleDeg(),
      .stabilization_weight = stab_w,
      .mode_transition_weight = mode_w,
      .imu_enabled = imu_->IsEnabled(),
  };
  Process(cfg, throttle, input);
}

void SlipAngleController::Process(const StabilizationConfig& cfg,
                                  float& throttle,
                                  const StabilizationInput& input) noexcept {
  if (input.stabilization_weight <= 0.0f) return;
  if (!input.imu_enabled) return;
  if (input.dt_ms == 0) return;

  const float dt_sec = static_cast<float>(input.dt_ms) * 0.001f;
  const float slip_error = cfg.slip_angle.target_deg - input.slip_angle_deg;
  const float pid_out = pid_.Step(slip_error, dt_sec);

  throttle = std::clamp(throttle + pid_out * input.stabilization_weight *
                                       input.mode_transition_weight,
                        -1.0f, 1.0f);
}

void SlipAngleController::SetGains(const StabilizationConfig& cfg) noexcept {
  pid_.SetGains({cfg.slip_angle.pid.kp, cfg.slip_angle.pid.ki,
                 cfg.slip_angle.pid.kd, cfg.slip_angle.pid.max_integral,
                 cfg.slip_angle.pid.max_correction});
}

// ─────────────────────────────────────────────────────────────────────────────
// OversteerGuard
// ─────────────────────────────────────────────────────────────────────────────

void OversteerGuard::Init(const VehicleEkf& ekf, const ImuHandler* imu) {
  assert(imu != nullptr && "OversteerGuard::Init() requires non-null imu");
  ekf_ = &ekf;
  imu_ = imu;
}

void OversteerGuard::Process(const StabilizationConfig& cfg, float& throttle,
                             uint32_t dt_ms, bool reduce_throttle) noexcept {
  if (!ekf_ || !imu_) return;
  const StabilizationInput input{
      .dt_ms = dt_ms,
      .speed_ms = ekf_->GetSpeedMs(),
      .slip_angle_deg = ekf_->GetSlipAngleDeg(),
      .yaw_rate_rps = ekf_->GetYawRate(),
      .imu_enabled = imu_->IsEnabled(),
  };
  Process(cfg, throttle, input, reduce_throttle);
}

void OversteerGuard::Process(const StabilizationConfig& cfg, float& throttle,
                             const StabilizationInput& input,
                             bool reduce_throttle) noexcept {
  if (!cfg.oversteer.warn_enabled) return;
  if (!input.imu_enabled) return;
  if (input.dt_ms == 0) return;

  const float dt_sec = static_cast<float>(input.dt_ms) * 0.001f;
  const float slip = input.slip_angle_deg;
  const float slip_rate = (slip - prev_slip_deg_) / dt_sec;
  prev_slip_deg_ = slip;

  // Занос невозможен без значимой угловой скорости рыскания. Yaw rate
  // измеряется напрямую гироскопом (не интегрируется), поэтому надёжен при
  // неподвижности. Без этой проверки EKF-дрейф vx/vy при стоянке даёт
  // ложный slip angle → ложное срабатывание.
  constexpr float kMinYawRateRad = 0.3f;  // ~17°/с
  // На малых скоростях EKF slip angle ненадёжен: vx/vy зашумлены,
  // atan2(vy,vx) скачет. Без энкодеров speed < 0.5 м/с — зона шума.
  constexpr float kMinSpeedMs = 0.5f;
  if (std::abs(input.yaw_rate_rps) < kMinYawRateRad ||
      input.speed_ms < kMinSpeedMs) {
    oversteer_active_ = false;
    // prev_slip_deg_ НЕ обнуляем: выше он уже обновлён текущим slip.
    // Обнуление давало ложный всплеск slip_rate = slip/dt на первом тике
    // после реактивации — детекция вырождалась в один порог slip (FW-R2).
    return;
  }

  oversteer_active_ = (std::abs(slip) > cfg.oversteer.slip_thresh_deg &&
                       std::abs(slip_rate) > cfg.oversteer.rate_thresh_deg_s);

  if (oversteer_active_ && cfg.oversteer.throttle_reduction > 0.0f &&
      reduce_throttle) {
    throttle *= (1.0f - cfg.oversteer.throttle_reduction);
  }
}

void OversteerGuard::Reset() noexcept {
  oversteer_active_ = false;
  prev_slip_deg_ = 0.0f;
}

StabilizationOutput StabilizationPipeline::Process(
    const StabilizationConfig& cfg, const ModeTraits& policy,
    const StabilizationInput& input) noexcept {
  StabilizationOutput output{
      .command = input.command,
      .motor_model_target_throttle = input.command.throttle,
  };

  if (policy.apply_input_limits) {
    kids_processor_.Process(cfg, output.command.throttle,
                            output.command.steering, input, nullptr,
                            /*apply_speed_limit=*/false);
  }

  if (policy.yaw_rate_active) {
    yaw_ctrl_.Process(cfg, output.command.steering, input,
                      output.command.throttle < 0.0f);
  }
  if (policy.pitch_comp_active) {
    pitch_ctrl_.Process(cfg, output.command.throttle, input);
  }
  if (policy.slip_angle_active) {
    slip_ctrl_.Process(cfg, output.command.throttle, input);
  }
  if (policy.oversteer_guard_active) {
    oversteer_guard_.Process(cfg, output.command.throttle, input,
                             policy.oversteer_reduces_throttle);
  }

  if (policy.apply_input_limits) {
    output.motor_model_target_throttle = output.command.throttle;
    kids_processor_.ApplySpeedLimit(cfg, output.command.throttle, input);
  }

  return output;
}

}  // namespace rc_vehicle
