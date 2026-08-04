#include "kids_mode_processor.hpp"

#include <algorithm>
#include <cmath>

#include "stabilization_pipeline.hpp"

namespace rc_vehicle {

namespace {

// Порог доверия к оценке скорости EKF (LOS-215): здоровая ковариация vx
// держится в районе 0.06..1.0 м²/с² (см. VehicleEkf::SetState/Predict), а при
// расходимости (LOS-217) улетает до сотен-тысяч. Выше порога speed limiter не
// применяется — иначе мусорная оценка «5-40 м/с при стоящей машине» рубит
// throttle почти до нуля независимо от силы нажатия газа.
//
// Вариацию одну недостаточно проверять (код-ревью PR #292): мотор-модельный
// якорь (LOS-233, motor_model_enabled=true по умолчанию) подаёт в EKF
// UpdateSpeed() каждый тик со слабым, но частым шумом speed_meas_noise=4.0 —
// это стягивает P_[0] обратно к шуму измерения независимо от того, доверять
// ли самой оценке x_[0]. Если EKF при этом клемпит скорость физическим
// максимумом (VehicleEkf::GuardState(), IsDiverged()==true), дисперсия может
// остаться ниже kSpeedTrustVarMax при заведомо испорченном состоянии —
// поэтому дополнительно гейтим по !ekf_->IsDiverged().
constexpr float kSpeedTrustVarMax = 4.0f;

// Верхний предел пропорционального снижения. 50% оставляет водителю
// управляемую тягу вместо почти полного обрыва команды.
constexpr float kSpeedReductionMax = 0.5f;

// Speed limiter включается выше max_speed_ms, а выключается только после
// возврата на эту величину ниже порога. Это исключает переключение на каждом
// тике из-за шума оценки скорости EKF.
constexpr float kSpeedLimitHysteresisMs = 0.1f;

}  // namespace

void KidsModeProcessor::Init(const VehicleEkf& ekf, const ImuHandler* imu) {
  ekf_ = &ekf;
  imu_ = imu;
  Reset();
}

void KidsModeProcessor::Process(const StabilizationConfig& cfg, float& throttle,
                                float& steering, uint32_t dt_ms,
                                float forward_accel,
                                float* throttle_before_speed_limit,
                                bool apply_speed_limit) noexcept {
  const StabilizationInput input{
      .dt_ms = dt_ms,
      .speed_ms = ekf_ ? ekf_->GetSpeedMs() : 0.0f,
      .slip_angle_deg = ekf_ ? ekf_->GetSlipAngleDeg() : 0.0f,
      .vx_variance = ekf_ ? ekf_->GetVxVariance() : 0.0f,
      .forward_accel_g = forward_accel,
      .imu_enabled = imu_ && imu_->IsEnabled(),
      .ekf_diverged = ekf_ && ekf_->IsDiverged(),
  };
  Process(cfg, throttle, steering, input, throttle_before_speed_limit,
          apply_speed_limit);
}

void KidsModeProcessor::Process(const StabilizationConfig& cfg, float& throttle,
                                float& steering,
                                const StabilizationInput& input,
                                float* throttle_before_speed_limit,
                                bool apply_speed_limit) noexcept {
  if (!IsActive(cfg)) {
    // Ограничители не применяются — сбрасываем статусы, иначе телеметрия
    // продолжила бы показывать сработавший лимитер после выхода из Kids или
    // снятия мастер-выключателя (LOS-286).
    anti_spin_active_ = false;
    accel_limit_active_ = false;
    speed_limit_active_ = false;
    return;
  }

  const auto& km = cfg.kids_mode;

  // ─────────────────────────────────────────────────────────────────────────
  // 1. Применить ограничения throttle/steering
  // ─────────────────────────────────────────────────────────────────────────

  // Ограничение газа: forward и reverse отдельно
  if (throttle > 0.0f) {
    throttle = std::min(throttle, km.throttle_limit);
  } else {
    throttle = std::max(throttle, -km.reverse_limit);
  }

  // Ограничение руля
  steering = std::clamp(steering, -km.steering_limit, km.steering_limit);

  // ─────────────────────────────────────────────────────────────────────────
  // 2. Anti-spin защита (снижение газа при заносе)
  // ─────────────────────────────────────────────────────────────────────────

  anti_spin_active_ = false;

  if (km.anti_spin_enabled && input.imu_enabled) {
    const float slip_deg = std::abs(input.slip_angle_deg);

    if (slip_deg > km.anti_spin_threshold_deg) {
      anti_spin_active_ = true;
      // Снизить газ на anti_spin_reduction процентов
      throttle *= (1.0f - km.anti_spin_reduction);
    }
  }

  // ─────────────────────────────────────────────────────────────────────────
  // 3. Ограничение по ускорению (IMU, не дрейфует)
  // ─────────────────────────────────────────────────────────────────────────

  accel_limit_active_ = false;

  if (km.accel_limit_enabled && throttle > 0.0f &&
      input.forward_accel_g > km.accel_threshold_g) {
    accel_limit_active_ = true;
    const float excess = input.forward_accel_g - km.accel_threshold_g;
    const float reduction =
        std::min(excess * km.accel_limit_gain, km.accel_max_reduction);
    throttle *= (1.0f - reduction);
  }

  if (apply_speed_limit) {
    if (throttle_before_speed_limit) {
      *throttle_before_speed_limit = throttle;
    }
    ApplySpeedLimit(cfg, throttle, input);
  } else {
    if (throttle_before_speed_limit) {
      *throttle_before_speed_limit = throttle;
    }
  }
}

void KidsModeProcessor::ApplySpeedLimit(const StabilizationConfig& cfg,
                                        float& throttle,
                                        uint32_t dt_ms) noexcept {
  const StabilizationInput input{
      .dt_ms = dt_ms,
      .speed_ms = ekf_ ? ekf_->GetSpeedMs() : 0.0f,
      .vx_variance = ekf_ ? ekf_->GetVxVariance() : 0.0f,
      .imu_enabled = imu_ && imu_->IsEnabled(),
      .ekf_diverged = ekf_ && ekf_->IsDiverged(),
  };
  ApplySpeedLimit(cfg, throttle, input);
}

void KidsModeProcessor::ApplySpeedLimit(
    const StabilizationConfig& cfg, float& throttle,
    const StabilizationInput& input) noexcept {
  if (!IsActive(cfg)) {
    speed_limit_active_ = false;
    return;
  }

  const auto& km = cfg.kids_mode;
  if (km.speed_limit_enabled && input.imu_enabled && throttle > 0.0f &&
      !input.ekf_diverged && input.vx_variance <= kSpeedTrustVarMax) {
    const float speed = input.speed_ms;
    const float release_speed = km.max_speed_ms - kSpeedLimitHysteresisMs;
    if (speed_limit_active_) {
      speed_limit_active_ = speed >= release_speed;
    } else {
      speed_limit_active_ = speed > km.max_speed_ms;
    }

    if (speed_limit_active_) {
      const float excess = std::max(speed - release_speed, 0.0f);
      const float reduction =
          std::min(excess * km.speed_limit_gain, kSpeedReductionMax);
      throttle *= (1.0f - reduction);
    }
  } else {
    speed_limit_active_ = false;
  }
}

void KidsModeProcessor::Reset() noexcept {
  anti_spin_active_ = false;
  accel_limit_active_ = false;
  speed_limit_active_ = false;
}

}  // namespace rc_vehicle
