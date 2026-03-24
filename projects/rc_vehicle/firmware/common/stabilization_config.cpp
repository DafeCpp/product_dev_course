#include "stabilization_config.hpp"

#include <algorithm>
#include <cstdint>

#include "drive_mode_registry.hpp"

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
  adaptive_accel_threshold_g =
      std::clamp(adaptive_accel_threshold_g, 0.05f, 0.5f);
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
  accel_threshold_g = std::clamp(accel_threshold_g, 0.05f, 0.5f);
  accel_limit_gain = std::clamp(accel_limit_gain, 0.5f, 10.0f);
  accel_max_reduction = std::clamp(accel_max_reduction, 0.0f, 1.0f);
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
      accel_limit_enabled = true;
      accel_threshold_g = 0.10f;
      accel_limit_gain = 5.0f;
      accel_max_reduction = 0.7f;
      break;

    case KidsPreset::Child:
      throttle_limit = 0.30f;
      reverse_limit = 0.20f;
      steering_limit = 0.7f;
      slew_throttle = 0.3f;
      slew_steering = 0.5f;
      anti_spin_threshold_deg = 10.0f;
      anti_spin_reduction = 0.7f;
      accel_limit_enabled = true;
      accel_threshold_g = 0.15f;
      accel_limit_gain = 3.0f;
      accel_max_reduction = 0.5f;
      break;

    case KidsPreset::Preteen:
      throttle_limit = 0.50f;
      reverse_limit = 0.35f;
      steering_limit = 0.85f;
      slew_throttle = 0.4f;
      slew_steering = 0.7f;
      anti_spin_threshold_deg = 15.0f;
      anti_spin_reduction = 0.5f;
      accel_limit_enabled = true;
      accel_threshold_g = 0.20f;
      accel_limit_gain = 2.0f;
      accel_max_reduction = 0.3f;
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
         static_cast<uint8_t>(mode) <= 4 &&
         slew_throttle >= 0.1f && slew_throttle <= 10.0f &&
         slew_steering >= 0.5f && slew_steering <= 10.0f &&
         steering_trim >= -0.1f && steering_trim <= 0.1f &&
         throttle_trim >= -0.1f && throttle_trim <= 0.1f;
}

void StabilizationConfig::Reset() noexcept {
  enabled = false;
  mode = DriveMode::Normal;
  fade_ms = 500;

  // Filter defaults
  filter.madgwick_beta = 0.1f;
  filter.lpf_cutoff_hz = 30.0f;
  filter.imu_sample_rate_hz = 500.0f;
  filter.adaptive_beta_enabled = true;
  filter.adaptive_accel_threshold_g = 0.2f;

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

  // Slew rate defaults
  slew_throttle = 0.5f;
  slew_steering = 3.0f;

  // Trim defaults
  steering_trim = 0.0f;
  throttle_trim = 0.0f;

  version = 3;
  magic = kStabilizationConfigMagic;
}

void StabilizationConfig::ApplyModeDefaults() noexcept {
  DriveModeRegistry::Get(mode).ApplyDefaults(*this);
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
  slew_throttle = std::clamp(slew_throttle, 0.1f, 10.0f);
  slew_steering = std::clamp(slew_steering, 0.5f, 10.0f);
  steering_trim = std::clamp(steering_trim, -0.1f, 0.1f);
  throttle_trim = std::clamp(throttle_trim, -0.1f, 0.1f);
}

}  // namespace rc_vehicle
