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
// |throttle|, при котором pitch-компенсация действует в полную силу (LOS-369).
constexpr float kPitchCompRampThrottle = 0.2f;
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
                                const StabilizationInput& input, bool reversing,
                                float ref_cutoff_hz) noexcept {
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
  // Опорная модель: на включении стартует с фактического yaw rate (без рывка
  // ошибки), дальше догоняет уставку фильтром 1-го порядка.
  if (!omega_ref_valid_) {
    omega_ref_dps_ = omega_actual;
    omega_ref_valid_ = true;
  }
  if (ref_cutoff_hz > 0.0f) {
    omega_ref_dps_ += (1.0f - std::exp(-kTwoPi * ref_cutoff_hz * dt_sec)) *
                      (omega_desired - omega_ref_dps_);
  } else {
    omega_ref_dps_ = omega_desired;
  }
  const float error_dps = omega_ref_dps_ - omega_actual;
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
  omega_ref_dps_ = 0.0f;
  omega_ref_valid_ = false;
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
  // Нейтраль (шум стика уже снят в SelectControlSource): компенсация не
  // трогается с места сама — в гору поехала бы вперёд, под гору назад, и
  // ненулевая команда блокировала бы ZUPT (LOS-369).
  if (throttle == 0.0f) return;

  // pitch_deg > 0 — нос ВНИЗ (конвенция Madgwick/TiltEstimator: pitch =
  // atan2(-ax, …), а на подъёме акселерометр видит ax = +sin). Подъём даёт
  // отрицательный pitch, поэтому знак меняем: gain > 0 добавляет газ в гору
  // (LOS-369).
  const float nose_up_deg = -input.pitch_deg;
  // Fix #8 (REFACTORING.md): std::clamp вместо ручного if/else
  const float correction =
      std::clamp(cfg.pitch_comp.gain * nose_up_deg,
                 -cfg.pitch_comp.max_correction, cfg.pitch_comp.max_correction);

  // У нейтрали коррекция нарастает линейно с |throttle|: иначе стик, едва
  // вышедший из мёртвой зоны, на склоне сразу получал бы полную добавку
  // (скачок 0 → max_correction).
  const float ramp =
      std::min(std::fabs(throttle) / kPitchCompRampThrottle, 1.0f);
  const float compensated = std::clamp(
      throttle + correction * ramp * input.stabilization_weight, -1.0f, 1.0f);
  // Компенсация не разворачивает команду: под гору газ вперёд снижается
  // максимум до нуля, но не превращается в задний ход/тормоз.
  throttle = (compensated * throttle < 0.0f) ? 0.0f : compensated;
}

// ─────────────────────────────────────────────────────────────────────────────
// RollCompensator
// ─────────────────────────────────────────────────────────────────────────────

void RollCompensator::Process(const StabilizationConfig& cfg, float& steering,
                              const StabilizationInput& input) noexcept {
  last_correction_ = 0.0f;
  if (!cfg.roll_comp.enabled) return;
  if (input.stabilization_weight <= 0.0f) return;
  if (!input.imu_enabled) return;
  if (input.command.throttle < 0.0f || input.vx_ms < 0.0f) return;

  const float speed_w = std::clamp(
      (input.vx_ms - kMinSpeedMs) / (kFullSpeedMs - kMinSpeedMs), 0.0f, 1.0f);
  constexpr float kG = 9.80665f;
  const float lat_g = std::fabs(input.vx_ms * input.yaw_rate_rps) / kG;
  const float turn_w =
      std::clamp((kZeroLatG - lat_g) / (kZeroLatG - kFullLatG), 0.0f, 1.0f);
  const float weight = speed_w * turn_w * input.stabilization_weight *
                       input.mode_transition_weight;
  if (weight <= 0.0f) return;

  const float correction =
      std::clamp(cfg.roll_comp.gain * input.roll_deg,
                 -cfg.roll_comp.max_correction, cfg.roll_comp.max_correction);
  const float before = steering;
  steering = std::clamp(steering + correction * weight, -1.0f, 1.0f);
  last_correction_ = steering - before;
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
      .yaw_rate_rps = ekf_->GetYawRate(),
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

  // Тормоз и реверс не регулируем: регулятор только снимает газ вперёд.
  if (throttle <= 0.0f) {
    pid_.Reset();
    return;
  }

  const float dt_sec = static_cast<float>(input.dt_ms) * 0.001f;
  const float slip_error =
      cfg.slip_angle.target_deg -
      DriftSlipDeg(input.slip_angle_deg, input.yaw_rate_rps);
  const float weight =
      input.stabilization_weight * input.mode_transition_weight;
  const float integral_before = pid_.GetIntegral();
  const float pid_out = pid_.Step(slip_error, dt_sec);
  // Пока стабилизация или режим плавно включаются, срез умножается на малый
  // вес; интеграл копит снятие с тем же весом, иначе к концу перехода он
  // успевал бы дойти до предела и резал бы газ до упора.
  if (weight < 1.0f) {
    pid_.SetIntegral(integral_before +
                     (pid_.GetIntegral() - integral_before) * weight);
  }
  // Стик задаёт предел газа: ниже уставки газ равен стику, выше регулятор
  // снимает газ. Поправка к стику в обе стороны держала занос лишь около
  // одного положения стика: угол заноса круто зависит от газа, и стик
  // работал как возмущение (LOS-380). Интеграл копит только снятие газа,
  // иначе после заноса он держал бы газ на пределе.
  if (pid_.GetIntegral() > 0.0f) pid_.SetIntegral(0.0f);
  const float cut = std::min(pid_out, 0.0f);

  // Пола наката, как у anti-spin Kids (LOS-389), здесь нет: с ним регулятор
  // не может снять газ ниже наката и не удерживает уставку (на газе 1.0 пик
  // заноса в SIL 61° против 39° без пола).
  const float reduced = throttle + cut * weight;
  // Anti-windup: газ уже снят до нуля, и дальше копить снятие незачем. Иначе
  // после заноса интеграл ещё долго резал бы стик.
  if (reduced <= 0.0f && slip_error < 0.0f) {
    pid_.SetIntegral(integral_before);
  }
  throttle = std::max(reduced, 0.0f);
}

float SlipAngleController::DriftSlipDeg(float slip_deg,
                                        float yaw_rate_rps) noexcept {
  // Пока поворота нет, направление заноса не определено: любой угол считаем
  // заносом, чтобы PID не разгонял машину при боковом сносе на прямой.
  if (std::abs(yaw_rate_rps) < kMinTurnYawRateRps) return std::abs(slip_deg);
  return yaw_rate_rps > 0.0f ? -slip_deg : slip_deg;
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
  // prev_slip_deg_ выше обновляется и здесь: обнуление давало ложный
  // всплеск slip_rate = slip/dt на первом тике после реактивации, и детекция
  // вырождалась в один порог slip (FW-R2).
  const bool turning = std::abs(input.yaw_rate_rps) >= kMinYawRateRad &&
                       input.speed_ms >= kMinSpeedMs;

  // Занос задней оси раньше виден по скорости роста угла: кузов вращается
  // быстрее, чем поворачивает траектория (ay/vx). Снос передней оси
  // (вращение медленнее траектории) — не занос.
  constexpr float kG = 9.80665f;
  const float trajectory_rate_rps =
      input.lateral_accel_g * kG / std::max(input.vx_ms, kMinSpeedMs);
  // Без скорости вдоль оси по EKF ay/vx — шум акселерометра (реальный лог
  // 2026-08-02: vx≈0 при |ay| до 2g).
  const bool rotation_valid = turning && input.vx_ms >= kMinSpeedMs;
  const float rotation_excess_rps =
      rotation_valid ? std::copysign(1.0f, input.yaw_rate_rps) *
                           (input.yaw_rate_rps - trajectory_rate_rps)
                     : 0.0f;
  rotation_excess_time_s_ =
      rotation_excess_rps > kRotationExcessRps
          ? std::min(rotation_excess_time_s_ + dt_sec, kRotationExcessHoldS)
          : 0.0f;

  const bool slip_oversteer =
      turning && std::abs(slip) > cfg.oversteer.slip_thresh_deg &&
      std::abs(slip_rate) > cfg.oversteer.rate_thresh_deg_s;
  const bool detected =
      slip_oversteer || rotation_excess_time_s_ >= kRotationExcessHoldS;

  // Занос держится, пока не погаснет: разовый срез отпускал газ, как только
  // вращение замедлялось, газ возвращался, и задняя ось срывалась снова
  // (LOS-380).
  if (detected) {
    oversteer_active_ = true;
    calm_time_s_ = 0.0f;
  } else if (!turning) {
    // Без вращения или на малой скорости заносу не из чего продолжаться.
    oversteer_active_ = false;
    calm_time_s_ = 0.0f;
  } else if (oversteer_active_) {
    const bool calm = rotation_excess_rps < 0.5f * kRotationExcessRps &&
                      std::abs(slip) < cfg.oversteer.slip_thresh_deg;
    calm_time_s_ = calm ? calm_time_s_ + dt_sec : 0.0f;
    if (calm_time_s_ >= kReleaseCalmS) oversteer_active_ = false;
  }

  const bool may_cut = cfg.oversteer.throttle_reduction > 0.0f &&
                       reduce_throttle && throttle > 0.0f;
  cutting_throttle_ = oversteer_active_ && may_cut;
  if (cutting_throttle_) {
    throttle *= (1.0f - cfg.oversteer.throttle_reduction);
    throttle_ceiling_ = throttle;
  } else if (throttle_ceiling_ < 1.0f) {
    // После заноса газ возвращается медленнее slew rate режима.
    throttle_ceiling_ =
        std::min(throttle_ceiling_ + kCeilingRecoveryPerS * dt_sec, 1.0f);
    if (may_cut) throttle = std::min(throttle, throttle_ceiling_);
  }
}

void OversteerGuard::Reset() noexcept {
  oversteer_active_ = false;
  cutting_throttle_ = false;
  rotation_excess_time_s_ = 0.0f;
  calm_time_s_ = 0.0f;
  throttle_ceiling_ = 1.0f;
  prev_slip_deg_ = 0.0f;
}

StabilizationOutput StabilizationPipeline::Process(
    const StabilizationConfig& cfg, const ModeTraits& policy,
    const StabilizationInput& input) noexcept {
  StabilizationOutput output{
      .command = input.command,
      .motor_model_target_throttle = input.command.throttle,
  };

  // В Kids компенсация тангажа идёт ДО ограничителей: предел газа,
  // anti-spin и accel limiter — защита ребёнка, и добавленный в гору газ не
  // должен её обходить (LOS-369). В остальных режимах порядок прежний.
  const bool pitch_before_limits =
      policy.pitch_comp_active && policy.apply_input_limits;
  if (pitch_before_limits) {
    pitch_ctrl_.Process(cfg, output.command.throttle, input);
  }

  if (policy.apply_input_limits) {
    kids_processor_.Process(cfg, output.command.throttle,
                            output.command.steering, input, nullptr,
                            /*apply_speed_limit=*/false);
  } else {
    // apply_input_limits есть только у Kids, поэтому в остальных режимах
    // Process() не вызывается вовсе — и его собственный сброс статусов (ветка
    // !IsActive) не отрабатывает. Без явного Reset() флаги лимитеров остались
    // бы в том состоянии, в каком их застал последний тик Kids, и телеметрия
    // приписывала бы срез газа всему остатку заезда (LOS-13).
    kids_processor_.Reset();
  }

  if (policy.yaw_rate_active) {
    // Направление берём и по исходной команде: в Kids компенсация тангажа на
    // подъёме может обнулить малый задний ход, но машина всё ещё едет назад,
    // и yaw-PID с инвертированной связью включать нельзя (LOS-369).
    const bool reversing =
        input.command.throttle < 0.0f || output.command.throttle < 0.0f;
    yaw_ctrl_.Process(cfg, output.command.steering, input, reversing,
                      policy.yaw_ref_cutoff_hz);
    if (policy.apply_input_limits) {
      kids_processor_.ClampToGripSteerCap(output.command.steering);
    }
  }
  if (policy.roll_comp_active) {
    // После yaw-ПИД: его уставка считается из руля пульта, и добавка вверх по
    // склону не должна читаться как команда водителя (LOS-386). В Kids
    // потолок руля по сцеплению распространяется и на неё.
    roll_ctrl_.Process(cfg, output.command.steering, input);
    if (policy.apply_input_limits) {
      kids_processor_.ClampToGripSteerCap(output.command.steering);
    }
  }
  if (policy.pitch_comp_active && !pitch_before_limits) {
    pitch_ctrl_.Process(cfg, output.command.throttle, input);
  }
  if (policy.slip_angle_active) {
    slip_ctrl_.Process(cfg, output.command.throttle, input);
  }
  if (policy.oversteer_guard_active) {
    // В Kids oversteer-гард сконфигурирован из kids_mode.anti_spin_*
    // (KidsModeStrategy::ApplyDefaults) и фактически дублирует anti-spin
    // процессора, то есть является частью защиты Kids. Поэтому снятый
    // мастер-выключатель ограничителей снимает и это срезание газа — иначе
    // «сырой проход команды» им бы всё равно нарушался (LOS-286). Детекция
    // заноса продолжает работать: IsOversteerActive() остаётся для
    // предупреждения в телеметрии.
    const bool kids_limiters_off =
        cfg.mode == DriveMode::Kids && !cfg.kids_mode.limiters_enabled;
    const float before_guard = output.command.throttle;
    oversteer_guard_.Process(
        cfg, output.command.throttle, input,
        policy.oversteer_reduces_throttle && !kids_limiters_off);
    // Срез гарда не опускает газ ниже наката: ниже него противо-ЭДС
    // тормозит ведущие колёса, и занос переходит в разворот (LOS-389). В Kids
    // это и пол наката, который оставил anti-spin.
    output.command.throttle = KidsModeProcessor::FloorSkidCut(
        cfg, input, before_guard, output.command.throttle);
    output.bypass_throttle_slew_down = oversteer_guard_.IsCuttingThrottle();
  }

  if (policy.apply_input_limits) {
    output.motor_model_target_throttle = output.command.throttle;
    kids_processor_.ApplySpeedLimit(cfg, output.command.throttle, input);
  }

  return output;
}

}  // namespace rc_vehicle
