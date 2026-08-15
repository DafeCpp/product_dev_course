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

// Минимальный "пол" итогового газа относительно команды водителя (LOS-247).
// Раньше это был единственный механизм ограничения (пропорциональное снижение
// упиралось в этот потолок почти сразу — LOS-285). Теперь это страховка
// только от ЧРЕЗМЕРНОГО СРЕЗА: если детерминированный потолок мотор-модели
// или адаптивный speed_trim_ (fallback без модели) окажутся ниже разумного —
// например, motor_speed_gain завышен и model_cap занижен, — лимитер не
// обрубит газ почти в ноль. От обратного рассинхрона (motor_speed_gain
// занижен, машина реально едет быстрее, чем предсказывает модель) этот пол
// не защищает: он пропускает газ, а не режет его (код-ревью PR #323).
constexpr float kMaxThrottleReductionFraction = 0.5f;

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
    speed_trim_ = 1.0f;
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
    speed_trim_ = 1.0f;
    return;
  }

  const auto& km = cfg.kids_mode;
  if (!(km.speed_limit_enabled && input.imu_enabled && throttle > 0.0f &&
        !input.ekf_diverged && input.vx_variance <= kSpeedTrustVarMax)) {
    speed_limit_active_ = false;
    speed_trim_ = 1.0f;
    return;
  }

  const float speed = input.speed_ms;
  const float release_speed = km.max_speed_ms - kSpeedLimitHysteresisMs;
  if (speed_limit_active_) {
    speed_limit_active_ = speed >= release_speed;
  } else {
    speed_limit_active_ = speed > km.max_speed_ms;
  }

  // motor_model_enabled одного недостаточно: пока активна калибровка
  // скорости (AutoDriveCoordinator::StartSpeedCalib), VehicleStateEstimator
  // гасит мотор-модельный якорь EKF (motor_model_active в
  // vehicle_state_estimator.cpp), и speed_ms там — честная IMU-интеграция, а
  // не эхо мотор-модели. Детерминированный потолок целится в формулу модели,
  // а не в то, что реально измеряет EKF в этот момент — тот же класс
  // рассинхрона, который этот PR устраняет для основного сценария (LOS-285;
  // код-ревью PR #323). Поэтому во время калибровки откатываемся на
  // адаптивный speed_trim_, как и при выключенной мотор-модели.
  const bool model_available =
      cfg.filter.motor_model_enabled && !input.speed_calibration_active &&
      cfg.filter.motor_deadzone < 1.0f && cfg.filter.motor_speed_gain > 0.0f;

  if (!model_available) {
    // Без мотор-модельного якоря speed_ms — честная IMU-интеграция (пусть и
    // дрейфующая), реальный, а не циклический сигнал. Медленно интегрируем
    // ошибку в speed_trim_ вместо мгновенной коррекции текущего тика:
    // speed_trim_ на входе в этот вызов не зависит от throttle этого же
    // тика, поэтому std::min(throttle, cap) ниже монотонен по стику —
    // "нажал сильнее" никогда не даёт меньше газа.
    //
    // Обновляем ВСЕГДА, пока открыт внешний гейт — а не только пока
    // speed_limit_active_ (гистерезис). Иначе это односторонняя трещотка:
    // error <= 0 всё время, пока лимитер активен, поэтому trim только падает;
    // без обновления вне активной фазы он никогда не восстановится к 1.0,
    // пока не случится полный выход из гейта (throttle<=0, EKF diverged и
    // т.п.) — затяжное превышение навсегда прижимает газ к страховочному
    // полу, даже когда скорость давно упала намного ниже max_speed_ms
    // (код-ревью PR #323). Ниже порога error > 0, и trim восстанавливается.
    const float dt_sec = static_cast<float>(input.dt_ms) * 0.001f;
    if (dt_sec > 0.0f) {
      const float error = release_speed - speed;
      speed_trim_ = std::clamp(
          speed_trim_ + error * km.speed_limit_gain * dt_sec, 0.0f, 1.0f);
    }
  }

  if (!speed_limit_active_) {
    return;
  }

  float cap;
  if (model_available) {
    // Детерминированный потолок: обращаем формулу мотор-модели, чтобы сам
    // потолок целился в max_speed_ms напрямую (LOS-285). speed_trim_
    // намеренно НЕ используется здесь как поправка поверх этого потолка:
    // пока мотор-модельный якорь активен, EKF speed_ms отслеживает
    // ДО-лимитерную команду газа (motor_model_target_throttle снимается до
    // ApplySpeedLimit, LOS-246), а не то, что реально получилось после
    // среза. Поэтому speed_ms никогда не отражает, сработал ли этот лимитер
    // — использовать её как корректирующий сигнал здесь означает воссоздать
    // ту же круговую зависимость в завуалированном виде: подтверждено на
    // closed-loop симуляции — при удержании газа adaptive trim стабильно
    // сползал к своему пределу и стирал model_cap независимо от результата
    // среза, просто чуть медленнее старой формулы.
    cap = cfg.filter.motor_deadzone + km.max_speed_ms *
                                          (1.0f - cfg.filter.motor_deadzone) /
                                          cfg.filter.motor_speed_gain;
  } else {
    cap = speed_trim_;
  }

  const float commanded = throttle;
  const float limited = std::min(commanded, cap);
  // Минимальный пол (LOS-247): не срезать больше
  // kMaxThrottleReductionFraction от команды водителя.
  throttle =
      std::max(limited, commanded * (1.0f - kMaxThrottleReductionFraction));
}

void KidsModeProcessor::Reset() noexcept {
  anti_spin_active_ = false;
  accel_limit_active_ = false;
  speed_limit_active_ = false;
  speed_trim_ = 1.0f;
}

}  // namespace rc_vehicle
