#include "motion_driver.hpp"

#include <algorithm>
#include <cmath>

namespace rc_vehicle {

void MotionDriver::Start(const Config& config) {
  config_ = config;
  pid_.SetGains(config_.pid_gains);
  pid_.Reset();
  phase_elapsed_sec_ = 0.0f;
  cruise_throttle_ = 0.0f;
  breakaway_detected_ = false;
  base_throttle_ = 0.0f;
  breakaway_confirm_count_ = 0;
  baseline_accel_g_ = 0.0f;
  settle_sum_ = 0.0;
  settle_count_ = 0;
  // Baseline нужен только PID-режиму: LinearRamp ускорение не читает.
  const bool needs_settle =
      config_.settle_ticks > 0 && config_.accel_mode == AccelMode::Pid;
  phase_ = needs_settle ? MotionPhase::Settle : MotionPhase::Accelerate;
}

void MotionDriver::Reset() {
  phase_ = MotionPhase::Idle;
  phase_elapsed_sec_ = 0.0f;
  cruise_throttle_ = 0.0f;
  breakaway_detected_ = false;
  base_throttle_ = 0.0f;
  breakaway_confirm_count_ = 0;
  baseline_accel_g_ = 0.0f;
  settle_sum_ = 0.0;
  settle_count_ = 0;
  pid_.Reset();
}

float MotionDriver::Update(float current_accel_g, float accel_magnitude,
                           float gyro_z_dps, float dt_sec) {
  if (dt_sec <= 0.0f) {
    return 0.0f;
  }

  switch (phase_) {
    case MotionPhase::Settle:
      phase_elapsed_sec_ += dt_sec;
      return UpdateSettle(current_accel_g);

    case MotionPhase::Accelerate:
      phase_elapsed_sec_ += dt_sec;
      return UpdateAccelerate(current_accel_g, dt_sec);

    case MotionPhase::Cruise:
      phase_elapsed_sec_ += dt_sec;
      return cruise_throttle_;

    case MotionPhase::Brake:
      phase_elapsed_sec_ += dt_sec;
      return UpdateBrake(accel_magnitude, gyro_z_dps);

    case MotionPhase::Idle:
    case MotionPhase::Stopped:
    default:
      return 0.0f;
  }
}

void MotionDriver::EndCruise() {
  if (phase_ == MotionPhase::Cruise) {
    TransitionTo(MotionPhase::Brake);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Private
// ─────────────────────────────────────────────────────────────────────────────

// Замер ускорения покоя перед разгоном. Газ не подаём: всё, что видит
// акселерометр в этой фазе, — статический офсет, который дальше вычитается.
float MotionDriver::UpdateSettle(float current_accel_g) {
  settle_sum_ += current_accel_g;
  ++settle_count_;

  if (settle_count_ >= config_.settle_ticks) {
    baseline_accel_g_ =
        static_cast<float>(settle_sum_ / static_cast<double>(settle_count_));
    TransitionTo(MotionPhase::Accelerate);
  }

  return 0.0f;
}

float MotionDriver::UpdateAccelerate(float current_accel_g, float dt_sec) {
  float throttle = 0.0f;

  // Ускорение относительно покоя: убирает статический офсет сигнала
  // (наклон площадки, погрешность accel_forward_vec), который иначе даёт
  // ложный отрыв на первом же тике и ломает уставку PI (LOS-214).
  const float accel_rel = current_accel_g - baseline_accel_g_;

  if (config_.accel_mode == AccelMode::Pid) {
    const auto& bk = config_.breakaway;

    if (!breakaway_detected_) {
      // ── Фаза A: open-loop рампа до отрыва ──
      throttle = bk.ramp_rate * phase_elapsed_sec_;
      throttle = std::min(throttle, bk.max_throttle);

      // Детекция отрыва: accel выше порога N тиков подряд
      if (accel_rel > bk.accel_thresh_g) {
        ++breakaway_confirm_count_;
      } else {
        breakaway_confirm_count_ = 0;
      }

      // Переход в PI: подтверждённый отрыв или fallback (рампа на максимуме)
      if (breakaway_confirm_count_ >= bk.confirm_ticks ||
          throttle >= bk.max_throttle) {
        breakaway_detected_ = true;
        base_throttle_ = throttle;
        pid_.Reset();
      }
    } else {
      // ── Фаза B: base_throttle + PI-коррекция ──
      float error = config_.target_value - accel_rel;
      float correction = pid_.Step(error, dt_sec);
      throttle = base_throttle_ + correction;
      throttle = std::clamp(throttle, 0.0f, config_.pid_gains.max_output);
    }
  } else {
    // LinearRamp: 0 → target_value за accel_duration_sec
    float t = std::min(phase_elapsed_sec_ / config_.accel_duration_sec, 1.0f);
    throttle = config_.target_value * t;

    // Минимальный рабочий газ: мёртвая зона ESC, ниже порога мотор не крутит.
    // Только для LinearRamp: в PID-режиме мёртвую зону проходит open-loop
    // рампа фазы A, а пол поверх PI-выхода мешал бы сходимости (FW-R5).
    if (config_.min_effective_throttle > 0.0f && throttle > 0.0f &&
        throttle < config_.min_effective_throttle) {
      throttle = config_.min_effective_throttle;
    }
  }

  // Переход в круиз по истечении времени разгона
  if (phase_elapsed_sec_ >= config_.accel_duration_sec) {
    cruise_throttle_ = throttle;
    TransitionTo(MotionPhase::Cruise);
  }

  return throttle;
}

float MotionDriver::UpdateBrake(float accel_magnitude, float gyro_z_dps) {
  bool accel_ok =
      std::abs(accel_magnitude - 1.0f) < config_.zupt.accel_thresh;

  bool gyro_ok = (config_.zupt.gyro_thresh <= 0.0f) ||
                 (std::abs(gyro_z_dps) < config_.zupt.gyro_thresh);

  bool stopped = accel_ok && gyro_ok;
  bool timeout = phase_elapsed_sec_ >= config_.brake_timeout_sec;

  if (stopped || timeout) {
    TransitionTo(MotionPhase::Stopped);
    return 0.0f;
  }

  return config_.brake_throttle;
}

void MotionDriver::TransitionTo(MotionPhase next) {
  phase_ = next;
  phase_elapsed_sec_ = 0.0f;

  if (next == MotionPhase::Brake) {
    pid_.Reset();
  }
}

}  // namespace rc_vehicle
