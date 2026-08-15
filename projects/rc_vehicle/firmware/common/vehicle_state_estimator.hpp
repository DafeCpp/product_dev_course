#pragma once

#include <cstdint>

#include "control_components.hpp"
#include "imu_calibration.hpp"
#include "madgwick_filter.hpp"
#include "stabilization_config.hpp"
#include "tilt_estimator.hpp"
#include "vehicle_ekf.hpp"

namespace rc_vehicle {

/**
 * Inputs that can change for one state-estimation tick.
 *
 * Sensor data is passed separately to Update() because CoM correction is part
 * of state estimation and intentionally updates the per-tick sensor snapshot.
 */
struct VehicleStateEstimatorInput {
  FilterConfig filter{};
  uint32_t dt_ms{0};
  // Post-stabilization command; ZUPT treats values inside motor_deadzone as
  // non-motion-capable residuals.
  float commanded_throttle{0.0f};
  float motor_model_throttle{0.0f};
  bool ekf_available{false};
  bool speed_calibration_active{false};
};

/** Immutable result consumed by control, stabilization and telemetry stages. */
struct VehicleStateEstimate {
  bool imu_valid{false};
  bool tilt_valid{false};
  float pitch_rad{0.0f};
  float roll_rad{0.0f};
  float yaw_rad{0.0f};
  float vx_ms{0.0f};
  float vy_ms{0.0f};
  float speed_ms{0.0f};
  float yaw_rate_rps{0.0f};
  float slip_angle_rad{0.0f};
  float forward_accel_g{0.0f};
  ZuptStatus zupt_status{ZuptStatus::NotEvaluated};
  bool ekf_diverged{false};
};

/**
 * Vehicle-frame state-estimation pipeline.
 *
 * ImuHandler remains the sensor/AHRS frontend: it reads and calibrates sensors,
 * tracks magnetometer freshness and updates Madgwick. This class owns the
 * vehicle-frame TiltEstimator/EKF orchestration and all of its cross-tick
 * state.
 */
class VehicleStateEstimator {
 public:
  VehicleStateEstimator(ImuCalibration& imu_calib, MadgwickFilter& madgwick,
                        VehicleEkf& ekf) noexcept
      : imu_calib_(imu_calib), madgwick_(madgwick), ekf_(ekf) {}

  /**
   * Update the estimate and apply CoM correction to this tick's sensor data.
   */
  [[nodiscard]] VehicleStateEstimate Update(
      SensorSnapshot& sensors,
      const VehicleStateEstimatorInput& input) noexcept;

  /** Reset state tied to the calibrated vehicle reference frame. */
  void OnReferenceFrameChanged() noexcept;

  /** Refresh only EKF-backed fields after an external EKF reset. */
  void RefreshEkfFields(VehicleStateEstimate& estimate) const noexcept;

 private:
  [[nodiscard]] VehicleStateEstimate BuildEstimate(
      bool imu_valid, bool tilt_valid, float pitch_rad, float roll_rad,
      float forward_accel_g) const noexcept;

  ImuCalibration& imu_calib_;
  MadgwickFilter& madgwick_;
  VehicleEkf& ekf_;

  TiltEstimator tilt_est_;
  float prev_gz_rad_s_{0.0f};
  float prev_vx_{0.0f};
  float a_lin_prev_g_{0.0f};
  uint32_t last_mag_sample_sequence_{0};
  bool tilt_was_enabled_{false};
};

}  // namespace rc_vehicle
