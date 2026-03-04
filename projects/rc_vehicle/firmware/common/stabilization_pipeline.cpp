#include "stabilization_pipeline.hpp"

#include <algorithm>
#include <cmath>

namespace rc_vehicle {

// ─────────────────────────────────────────────────────────────────────────────
// YawRateController
// ─────────────────────────────────────────────────────────────────────────────

void YawRateController::Init(const StabilizationConfig& cfg,
                             const VehicleEkf& ekf, const ImuHandler* imu) {
  cfg_ = &cfg;
  ekf_ = &ekf;
  imu_ = imu;
  SetGains(cfg);
}

void YawRateController::Process(float& steering, float stab_w, float mode_w,
                                uint32_t dt_ms) noexcept {
  if (!cfg_ || !ekf_ || !imu_) return;
  if (cfg_->mode == DriveMode::Drift) return;  // Yaw PID отключён в Drift mode
  if (stab_w <= 0.0f) return;
  if (!imu_->IsEnabled()) return;
  if (dt_ms == 0) return;

  const float dt_sec = static_cast<float>(dt_ms) * 0.001f;
  const float omega_desired = cfg_->yaw_rate.steer_to_yaw_rate_dps * steering;
  const float omega_actual = imu_->GetFilteredGyroZ();
  const float pid_out = pid_.Step(omega_desired - omega_actual, dt_sec);

  // Adaptive PID: масштабирование выхода ПИД по скорости из EKF (Phase 4.1)
  float adaptive_scale = 1.0f;
  if (cfg_->adaptive.enabled && cfg_->adaptive.speed_ref_ms > 0.0f) {
    adaptive_scale =
        std::clamp(ekf_->GetSpeedMs() / cfg_->adaptive.speed_ref_ms,
                   cfg_->adaptive.scale_min, cfg_->adaptive.scale_max);
  }

  steering = std::clamp(steering + pid_out * stab_w * mode_w * adaptive_scale,
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

void PitchCompensator::Init(const StabilizationConfig& cfg,
                            const MadgwickFilter& madgwick,
                            const ImuHandler* imu) {
  cfg_ = &cfg;
  madgwick_ = &madgwick;
  imu_ = imu;
}

void PitchCompensator::Process(float& throttle, float stab_w) noexcept {
  if (!cfg_ || !madgwick_ || !imu_) return;
  if (!cfg_->pitch_comp.enabled) return;
  if (stab_w <= 0.0f) return;
  if (!imu_->IsEnabled()) return;

  float pitch_deg = 0.0f, roll_deg = 0.0f, yaw_deg = 0.0f;
  madgwick_->GetEulerDeg(pitch_deg, roll_deg, yaw_deg);

  // Fix #8 (REFACTORING.md): std::clamp вместо ручного if/else
  const float correction = std::clamp(cfg_->pitch_comp.gain * pitch_deg,
                                      -cfg_->pitch_comp.max_correction,
                                      cfg_->pitch_comp.max_correction);

  throttle = std::clamp(throttle + correction * stab_w, -1.0f, 1.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// SlipAngleController
// ─────────────────────────────────────────────────────────────────────────────

void SlipAngleController::Init(const StabilizationConfig& cfg,
                               const VehicleEkf& ekf, const ImuHandler* imu) {
  cfg_ = &cfg;
  ekf_ = &ekf;
  imu_ = imu;
  SetGains(cfg);
}

void SlipAngleController::Process(float& throttle, float stab_w, float mode_w,
                                  uint32_t dt_ms) noexcept {
  if (!cfg_ || !ekf_ || !imu_) return;
  if (cfg_->mode != DriveMode::Drift) return;  // Только в Drift mode
  if (stab_w <= 0.0f) return;
  if (!imu_->IsEnabled()) return;
  if (dt_ms == 0) return;

  const float dt_sec = static_cast<float>(dt_ms) * 0.001f;
  const float slip_error =
      cfg_->slip_angle.target_deg - ekf_->GetSlipAngleDeg();
  const float pid_out = pid_.Step(slip_error, dt_sec);

  throttle = std::clamp(throttle + pid_out * stab_w * mode_w, -1.0f, 1.0f);
}

void SlipAngleController::SetGains(const StabilizationConfig& cfg) noexcept {
  pid_.SetGains({cfg.slip_angle.pid.kp, cfg.slip_angle.pid.ki,
                 cfg.slip_angle.pid.kd, cfg.slip_angle.pid.max_integral,
                 cfg.slip_angle.pid.max_correction});
}

// ─────────────────────────────────────────────────────────────────────────────
// OversteerGuard
// ─────────────────────────────────────────────────────────────────────────────

void OversteerGuard::Init(const StabilizationConfig& cfg, const VehicleEkf& ekf,
                          const ImuHandler* imu) {
  cfg_ = &cfg;
  ekf_ = &ekf;
  imu_ = imu;
}

void OversteerGuard::Process(float& throttle, uint32_t dt_ms) noexcept {
  if (!cfg_ || !ekf_ || !imu_) return;
  if (!cfg_->oversteer.warn_enabled) return;
  if (!imu_->IsEnabled()) return;
  if (dt_ms == 0) return;

  const float dt_sec = static_cast<float>(dt_ms) * 0.001f;
  const float slip = ekf_->GetSlipAngleDeg();
  const float slip_rate = (slip - prev_slip_deg_) / dt_sec;
  prev_slip_deg_ = slip;

  oversteer_active_ = (std::abs(slip) > cfg_->oversteer.slip_thresh_deg &&
                       std::abs(slip_rate) > cfg_->oversteer.rate_thresh_deg_s);

  if (oversteer_active_ && cfg_->oversteer.throttle_reduction > 0.0f &&
      cfg_->mode != DriveMode::Drift) {
    throttle *= (1.0f - cfg_->oversteer.throttle_reduction);
  }
}

void OversteerGuard::Reset() noexcept {
  oversteer_active_ = false;
  prev_slip_deg_ = 0.0f;
}

}  // namespace rc_vehicle
