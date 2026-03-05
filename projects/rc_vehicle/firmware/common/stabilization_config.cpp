#include "stabilization_config.hpp"

#include <algorithm>
#include <cstdint>

namespace rc_vehicle {

// ============================================================================
// PidConfig
// ============================================================================

void PidConfig::Clamp() noexcept {
  if (kp < 0.0f) kp = 0.0f;
  if (ki < 0.0f) ki = 0.0f;
  if (kd < 0.0f) kd = 0.0f;
  if (max_integral < 0.0f) max_integral = 0.0f;
  max_correction = std::clamp(max_correction, 0.0f, 1.0f);
}

// ============================================================================
// FilterConfig
// ============================================================================

void FilterConfig::Clamp() noexcept {
  madgwick_beta = std::clamp(madgwick_beta, 0.01f, 1.0f);
  lpf_cutoff_hz = std::clamp(lpf_cutoff_hz, 5.0f, 100.0f);
  if (imu_sample_rate_hz < 100.0f) imu_sample_rate_hz = 100.0f;
}

// ============================================================================
// AdaptiveConfig
// ============================================================================

void AdaptiveConfig::Clamp() noexcept {
  speed_ref_ms = std::clamp(speed_ref_ms, 0.1f, 10.0f);
  scale_min = std::clamp(scale_min, 0.1f, 1.0f);
  scale_max = std::clamp(scale_max, 1.0f, 5.0f);
}

// ============================================================================
// OversteerConfig
// ============================================================================

void OversteerConfig::Clamp() noexcept {
  slip_thresh_deg = std::clamp(slip_thresh_deg, 5.0f, 45.0f);
  rate_thresh_deg_s = std::clamp(rate_thresh_deg_s, 10.0f, 500.0f);
  throttle_reduction = std::clamp(throttle_reduction, 0.0f, 1.0f);
}

// ============================================================================
// YawRateConfig
// ============================================================================

void YawRateConfig::Clamp() noexcept {
  pid.Clamp();
  steer_to_yaw_rate_dps = std::clamp(steer_to_yaw_rate_dps, 10.0f, 360.0f);
}

// ============================================================================
// SlipAngleConfig
// ============================================================================

void SlipAngleConfig::Clamp() noexcept {
  pid.Clamp();
  // Дополнительные ограничения для slip PID
  if (pid.kp > 1.0f) pid.kp = 1.0f;
  target_deg = std::clamp(target_deg, -45.0f, 45.0f);
}

// ============================================================================
// PitchCompensationConfig
// ============================================================================

void PitchCompensationConfig::Clamp() noexcept {
  gain = std::clamp(gain, 0.0f, 0.05f);
  max_correction = std::clamp(max_correction, 0.0f, 0.5f);
}

// ============================================================================
// KidsModeConfig
// ============================================================================

void KidsModeConfig::Clamp() noexcept {
  throttle_limit = std::clamp(throttle_limit, 0.1f, 1.0f);
  reverse_limit = std::clamp(reverse_limit, 0.1f, 1.0f);
  steering_limit = std::clamp(steering_limit, 0.3f, 1.0f);
  slew_throttle = std::clamp(slew_throttle, 0.1f, 2.0f);
  slew_steering = std::clamp(slew_steering, 0.2f, 3.0f);
  anti_spin_threshold_deg = std::clamp(anti_spin_threshold_deg, 5.0f, 45.0f);
  anti_spin_reduction = std::clamp(anti_spin_reduction, 0.0f, 1.0f);
}

void KidsModeConfig::ApplyPreset(KidsPreset preset) noexcept {
  switch (preset) {
    case KidsPreset::Toddler:
      throttle_limit = 0.15f;
      reverse_limit = 0.10f;
      steering_limit = 0.5f;
      slew_throttle = 0.2f;
      slew_steering = 0.3f;
      anti_spin_threshold_deg = 5.0f;
      anti_spin_reduction = 0.8f;
      break;

    case KidsPreset::Child:
      throttle_limit = 0.30f;
      reverse_limit = 0.20f;
      steering_limit = 0.7f;
      slew_throttle = 0.3f;
      slew_steering = 0.5f;
      anti_spin_threshold_deg = 10.0f;
      anti_spin_reduction = 0.7f;
      break;

    case KidsPreset::Preteen:
      throttle_limit = 0.50f;
      reverse_limit = 0.35f;
      steering_limit = 0.85f;
      slew_throttle = 0.4f;
      slew_steering = 0.7f;
      anti_spin_threshold_deg = 15.0f;
      anti_spin_reduction = 0.5f;
      break;

    default:
      break;  // Custom — не меняем
  }
}

// ============================================================================
// StabilizationConfig
// ============================================================================

bool StabilizationConfig::IsValid() const noexcept {
  return magic == kStabilizationConfigMagic && filter.IsValid() &&
         yaw_rate.IsValid() && slip_angle.IsValid() && adaptive.IsValid() &&
         oversteer.IsValid() && pitch_comp.IsValid() && kids_mode.IsValid() &&
         static_cast<uint8_t>(mode) <= 4;
}

void StabilizationConfig::Reset() noexcept {
  enabled = false;
  mode = DriveMode::Normal;
  fade_ms = 500;

  // Filter defaults
  filter.madgwick_beta = 0.1f;
  filter.lpf_cutoff_hz = 30.0f;
  filter.imu_sample_rate_hz = 500.0f;

  // Yaw rate defaults
  yaw_rate.pid.kp = 0.1f;
  yaw_rate.pid.ki = 0.0f;
  yaw_rate.pid.kd = 0.005f;
  yaw_rate.pid.max_integral = 0.5f;
  yaw_rate.pid.max_correction = 0.3f;
  yaw_rate.steer_to_yaw_rate_dps = 90.0f;

  // Slip angle defaults
  slip_angle.pid.kp = 0.0f;
  slip_angle.pid.ki = 0.0f;
  slip_angle.pid.kd = 0.0f;
  slip_angle.pid.max_integral = 5.0f;
  slip_angle.pid.max_correction = 0.0f;
  slip_angle.target_deg = 0.0f;

  // Adaptive defaults
  adaptive.enabled = false;
  adaptive.speed_ref_ms = 1.5f;
  adaptive.scale_min = 0.5f;
  adaptive.scale_max = 2.0f;

  // Oversteer defaults
  oversteer.warn_enabled = false;
  oversteer.slip_thresh_deg = 20.0f;
  oversteer.rate_thresh_deg_s = 50.0f;
  oversteer.throttle_reduction = 0.0f;

  // Pitch compensation defaults
  pitch_comp.enabled = false;
  pitch_comp.gain = 0.01f;
  pitch_comp.max_correction = 0.25f;

  // Kids mode defaults
  kids_mode.throttle_limit = 0.3f;
  kids_mode.reverse_limit = 0.2f;
  kids_mode.steering_limit = 0.7f;
  kids_mode.slew_throttle = 0.3f;
  kids_mode.slew_steering = 0.5f;
  kids_mode.anti_spin_enabled = true;
  kids_mode.anti_spin_threshold_deg = 10.0f;
  kids_mode.anti_spin_reduction = 0.7f;

  version = 3;
  magic = kStabilizationConfigMagic;
}

void StabilizationConfig::ApplyModeDefaults() noexcept {
  switch (mode) {
    case DriveMode::Sport:  // быстрый отклик, лёгкий slip assist
      yaw_rate.pid.kp = 0.20f;
      yaw_rate.pid.ki = 0.01f;
      yaw_rate.pid.kd = 0.010f;
      yaw_rate.pid.max_integral = 1.0f;
      yaw_rate.pid.max_correction = 0.40f;
      yaw_rate.steer_to_yaw_rate_dps = 120.0f;

      pitch_comp.gain = 0.02f;
      pitch_comp.max_correction = 0.30f;

      slip_angle.target_deg = 5.0f;
      slip_angle.pid.kp = 0.003f;
      slip_angle.pid.ki = 0.0f;
      slip_angle.pid.kd = 0.001f;
      slip_angle.pid.max_integral = 5.0f;
      slip_angle.pid.max_correction = 0.15f;
      break;

    case DriveMode::Drift:  // мягкая коррекция yaw, slip angle PID включён
      yaw_rate.pid.kp = 0.05f;
      yaw_rate.pid.ki = 0.00f;
      yaw_rate.pid.kd = 0.002f;
      yaw_rate.pid.max_integral = 0.3f;
      yaw_rate.pid.max_correction = 0.20f;
      yaw_rate.steer_to_yaw_rate_dps = 60.0f;

      pitch_comp.gain = 0.005f;
      pitch_comp.max_correction = 0.15f;

      slip_angle.target_deg = 15.0f;
      slip_angle.pid.kp = 0.008f;
      slip_angle.pid.ki = 0.0f;
      slip_angle.pid.kd = 0.002f;
      slip_angle.pid.max_integral = 5.0f;
      slip_angle.pid.max_correction = 0.25f;
      break;

    case DriveMode::Kids:  // усиленная стабилизация, помощь в управлении
      // Yaw rate: увеличенный kp для лучшего контроля
      yaw_rate.pid.kp = 0.15f;
      yaw_rate.pid.ki = 0.005f;
      yaw_rate.pid.kd = 0.008f;
      yaw_rate.pid.max_integral = 0.8f;
      yaw_rate.pid.max_correction = 0.35f;
      yaw_rate.steer_to_yaw_rate_dps = 75.0f;

      // Pitch compensation: включена для помощи на склонах
      pitch_comp.enabled = true;
      pitch_comp.gain = 0.015f;
      pitch_comp.max_correction = 0.30f;

      // Slip angle: выключен (не нужен в Kids Mode)
      slip_angle.target_deg = 0.0f;
      slip_angle.pid.kp = 0.0f;
      slip_angle.pid.ki = 0.0f;
      slip_angle.pid.kd = 0.0f;
      slip_angle.pid.max_integral = 5.0f;
      slip_angle.pid.max_correction = 0.0f;

      // Oversteer guard: агрессивное снижение газа
      oversteer.warn_enabled = true;
      oversteer.slip_thresh_deg = kids_mode.anti_spin_threshold_deg;
      oversteer.rate_thresh_deg_s = 30.0f;
      oversteer.throttle_reduction = kids_mode.anti_spin_reduction;

      // Adaptive PID: включён для лучшего контроля на разных скоростях
      adaptive.enabled = true;
      adaptive.speed_ref_ms = 1.0f;
      adaptive.scale_min = 0.6f;
      adaptive.scale_max = 1.5f;
      break;

    case DriveMode::DirectLaw:  // прямое управление: стабилизация не используется
      enabled = false;
      yaw_rate.pid.kp = 0.0f;
      yaw_rate.pid.ki = 0.0f;
      yaw_rate.pid.kd = 0.0f;
      yaw_rate.pid.max_correction = 0.0f;
      pitch_comp.enabled = false;
      pitch_comp.gain = 0.0f;
      slip_angle.target_deg = 0.0f;
      slip_angle.pid.kp = 0.0f;
      slip_angle.pid.ki = 0.0f;
      slip_angle.pid.kd = 0.0f;
      slip_angle.pid.max_correction = 0.0f;
      break;

    default:  // Normal: базовые параметры, slip PID выключен
      yaw_rate.pid.kp = 0.10f;
      yaw_rate.pid.ki = 0.00f;
      yaw_rate.pid.kd = 0.005f;
      yaw_rate.pid.max_integral = 0.5f;
      yaw_rate.pid.max_correction = 0.30f;
      yaw_rate.steer_to_yaw_rate_dps = 90.0f;

      pitch_comp.gain = 0.01f;
      pitch_comp.max_correction = 0.25f;

      slip_angle.target_deg = 0.0f;
      slip_angle.pid.kp = 0.0f;
      slip_angle.pid.ki = 0.0f;
      slip_angle.pid.kd = 0.0f;
      slip_angle.pid.max_integral = 5.0f;
      slip_angle.pid.max_correction = 0.0f;
      break;
  }
}

void StabilizationConfig::Clamp() noexcept {
  if (fade_ms > 5000) fade_ms = 5000;
  if (static_cast<uint8_t>(mode) > 4) mode = DriveMode::Normal;

  filter.Clamp();
  yaw_rate.Clamp();
  slip_angle.Clamp();
  adaptive.Clamp();
  oversteer.Clamp();
  pitch_comp.Clamp();
  kids_mode.Clamp();
}

}  // namespace rc_vehicle
