#include "stabilization_pipeline.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace rc_vehicle {

namespace {
// FW-R17: рулевая yaw-rate-стабилизация осмысленна только в движении. Ниже этой
// скорости (EKF) руль не влияет на рысканье, поэтому контур не подмешивается —
// иначе он гоняется за шумом гироскопа (дрожание руля) и срывается в упор ±1.0
// на толчок. Порог с запасом над дрейфом оценки скорости; при нужде вынести в
// конфиг. См. tasks/FW-R17-phantom-steering-after-boot.md.
constexpr float kMinStabSpeedMs = 0.2f;
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
  if (input.stabilization_weight <= 0.0f) return;
  if (!input.imu_enabled) return;
  if (input.dt_ms == 0) return;

  // FW-R17: на стоянке/околонулевой скорости не подмешиваем коррекцию (руль
  // проходит как есть) и держим PID в сбросе — анти-windup при остановках.
  if (input.speed_ms < kMinStabSpeedMs) {
    pid_.Reset();
    return;
  }

  // FW-R22: в реверсе связь руль→рыскание инвертируется, и yaw-rate обратная
  // связь становится положительной → автоколебания руля (hunting) при том, что
  // руль водителем не трогается. Стабилизацию в реверсе отключаем: руль
  // проходит как есть, PID в сбросе. Направление берём по знаку команды газа,
  // т.к. EKF vx ненадёжен (IMU-only, дрейф: vx_var в логах доходит до 193).
  if (reversing) {
    pid_.Reset();
    return;
  }

  const float dt_sec = static_cast<float>(input.dt_ms) * 0.001f;
  const float omega_desired = cfg.yaw_rate.steer_to_yaw_rate_dps * steering;
  const float omega_actual = input.filtered_gyro_z_dps;
  const float pid_out = pid_.Step(omega_desired - omega_actual, dt_sec);

  // Adaptive PID: масштабирование выхода ПИД по скорости из EKF (Phase 4.1)
  float adaptive_scale = 1.0f;
  if (cfg.adaptive.enabled && cfg.adaptive.speed_ref_ms > 0.0f) {
    adaptive_scale = std::clamp(input.speed_ms / cfg.adaptive.speed_ref_ms,
                                cfg.adaptive.scale_min, cfg.adaptive.scale_max);
  }

  steering =
      std::clamp(steering + pid_out * input.stabilization_weight *
                                input.mode_transition_weight * adaptive_scale,
                 -1.0f, 1.0f);
}

void YawRateController::SetGains(const StabilizationConfig& cfg) noexcept {
  pid_.SetGains({cfg.yaw_rate.pid.kp, cfg.yaw_rate.pid.ki, cfg.yaw_rate.pid.kd,
                 cfg.yaw_rate.pid.max_integral,
                 cfg.yaw_rate.pid.max_correction});
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
