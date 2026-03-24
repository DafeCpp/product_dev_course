#include "drive_modes.hpp"

namespace rc_vehicle {

// ═════════════════════════════════════════════════════════════════════════════
// NormalModeStrategy
// ═════════════════════════════════════════════════════════════════════════════

void NormalModeStrategy::ApplyDefaults(
    StabilizationConfig& cfg) const noexcept {
  cfg.yaw_rate.pid.kp = 0.10f;
  cfg.yaw_rate.pid.ki = 0.00f;
  cfg.yaw_rate.pid.kd = 0.005f;
  cfg.yaw_rate.pid.max_integral = 0.5f;
  cfg.yaw_rate.pid.max_correction = 0.30f;
  cfg.yaw_rate.steer_to_yaw_rate_dps = 90.0f;

  cfg.pitch_comp.gain = 0.01f;
  cfg.pitch_comp.max_correction = 0.25f;

  cfg.slip_angle.target_deg = 0.0f;
  cfg.slip_angle.pid.kp = 0.0f;
  cfg.slip_angle.pid.ki = 0.0f;
  cfg.slip_angle.pid.kd = 0.0f;
  cfg.slip_angle.pid.max_integral = 5.0f;
  cfg.slip_angle.pid.max_correction = 0.0f;

  cfg.slew_throttle = 0.5f;
  cfg.slew_steering = 3.0f;
}

// ═════════════════════════════════════════════════════════════════════════════
// SportModeStrategy
// ═════════════════════════════════════════════════════════════════════════════

void SportModeStrategy::ApplyDefaults(
    StabilizationConfig& cfg) const noexcept {
  cfg.yaw_rate.pid.kp = 0.20f;
  cfg.yaw_rate.pid.ki = 0.01f;
  cfg.yaw_rate.pid.kd = 0.010f;
  cfg.yaw_rate.pid.max_integral = 1.0f;
  cfg.yaw_rate.pid.max_correction = 0.40f;
  cfg.yaw_rate.steer_to_yaw_rate_dps = 120.0f;

  cfg.pitch_comp.gain = 0.02f;
  cfg.pitch_comp.max_correction = 0.30f;

  cfg.slip_angle.target_deg = 5.0f;
  cfg.slip_angle.pid.kp = 0.003f;
  cfg.slip_angle.pid.ki = 0.0f;
  cfg.slip_angle.pid.kd = 0.001f;
  cfg.slip_angle.pid.max_integral = 5.0f;
  cfg.slip_angle.pid.max_correction = 0.15f;

  cfg.slew_throttle = 1.0f;
  cfg.slew_steering = 5.0f;
}

// ═════════════════════════════════════════════════════════════════════════════
// DriftModeStrategy
// ═════════════════════════════════════════════════════════════════════════════

void DriftModeStrategy::ApplyDefaults(
    StabilizationConfig& cfg) const noexcept {
  cfg.yaw_rate.pid.kp = 0.05f;
  cfg.yaw_rate.pid.ki = 0.00f;
  cfg.yaw_rate.pid.kd = 0.002f;
  cfg.yaw_rate.pid.max_integral = 0.3f;
  cfg.yaw_rate.pid.max_correction = 0.20f;
  cfg.yaw_rate.steer_to_yaw_rate_dps = 60.0f;

  cfg.pitch_comp.gain = 0.005f;
  cfg.pitch_comp.max_correction = 0.15f;

  cfg.slip_angle.target_deg = 15.0f;
  cfg.slip_angle.pid.kp = 0.008f;
  cfg.slip_angle.pid.ki = 0.0f;
  cfg.slip_angle.pid.kd = 0.002f;
  cfg.slip_angle.pid.max_integral = 5.0f;
  cfg.slip_angle.pid.max_correction = 0.25f;

  cfg.slew_throttle = 0.8f;
  cfg.slew_steering = 4.0f;
}

// ═════════════════════════════════════════════════════════════════════════════
// KidsModeStrategy
// ═════════════════════════════════════════════════════════════════════════════

void KidsModeStrategy::ApplyDefaults(
    StabilizationConfig& cfg) const noexcept {
  cfg.yaw_rate.pid.kp = 0.15f;
  cfg.yaw_rate.pid.ki = 0.005f;
  cfg.yaw_rate.pid.kd = 0.008f;
  cfg.yaw_rate.pid.max_integral = 0.8f;
  cfg.yaw_rate.pid.max_correction = 0.35f;
  cfg.yaw_rate.steer_to_yaw_rate_dps = 75.0f;

  cfg.pitch_comp.enabled = true;
  cfg.pitch_comp.gain = 0.015f;
  cfg.pitch_comp.max_correction = 0.30f;

  cfg.slip_angle.target_deg = 0.0f;
  cfg.slip_angle.pid.kp = 0.0f;
  cfg.slip_angle.pid.ki = 0.0f;
  cfg.slip_angle.pid.kd = 0.0f;
  cfg.slip_angle.pid.max_integral = 5.0f;
  cfg.slip_angle.pid.max_correction = 0.0f;

  cfg.oversteer.warn_enabled = true;
  cfg.oversteer.slip_thresh_deg = cfg.kids_mode.anti_spin_threshold_deg;
  cfg.oversteer.rate_thresh_deg_s = 30.0f;
  cfg.oversteer.throttle_reduction = cfg.kids_mode.anti_spin_reduction;

  cfg.adaptive.enabled = true;
  cfg.adaptive.speed_ref_ms = 1.0f;
  cfg.adaptive.scale_min = 0.6f;
  cfg.adaptive.scale_max = 1.5f;

  cfg.slew_throttle = 0.3f;
  cfg.slew_steering = 1.5f;
}

// ═════════════════════════════════════════════════════════════════════════════
// DirectLawStrategy
// ═════════════════════════════════════════════════════════════════════════════

void DirectLawStrategy::ApplyDefaults(
    StabilizationConfig& cfg) const noexcept {
  cfg.enabled = false;
  cfg.yaw_rate.pid.kp = 0.0f;
  cfg.yaw_rate.pid.ki = 0.0f;
  cfg.yaw_rate.pid.kd = 0.0f;
  cfg.yaw_rate.pid.max_correction = 0.0f;
  cfg.pitch_comp.enabled = false;
  cfg.pitch_comp.gain = 0.0f;
  cfg.slip_angle.target_deg = 0.0f;
  cfg.slip_angle.pid.kp = 0.0f;
  cfg.slip_angle.pid.ki = 0.0f;
  cfg.slip_angle.pid.kd = 0.0f;
  cfg.slip_angle.pid.max_correction = 0.0f;
}

}  // namespace rc_vehicle
