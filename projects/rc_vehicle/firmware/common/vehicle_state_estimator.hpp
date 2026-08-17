#pragma once

#include <cstdint>

#include "control_components.hpp"
#include "imu_calibration.hpp"
#include "madgwick_filter.hpp"
#include "stabilization_config.hpp"
#include "tilt_estimator.hpp"
#include "vehicle_ekf.hpp"

namespace rc_vehicle {

#ifdef RC_PROFILE_LOOP
class VehicleControlPlatform;

/** Accumulated timing for one estimator stage in a profiling window. */
struct VehicleStateEstimatorStageProfile {
  uint64_t total_us{0};
  uint64_t max_us{0};
  uint32_t calls{0};

  void Record(uint64_t elapsed_us) noexcept {
    total_us += elapsed_us;
    if (elapsed_us > max_us) max_us = elapsed_us;
    ++calls;
  }
};

/** Profiling-only breakdown of the vehicle state-estimation pipeline. */
struct VehicleStateEstimatorProfile {
  VehicleStateEstimatorStageProfile com_offset;
  VehicleStateEstimatorStageProfile rotate;
  VehicleStateEstimatorStageProfile tilt;
  VehicleStateEstimatorStageProfile imu;
  VehicleStateEstimatorStageProfile speed;
  VehicleStateEstimatorStageProfile nhc;
  VehicleStateEstimatorStageProfile heading;
};
#endif

/**
 * Inputs that can change for one state-estimation tick.
 *
 * Sensor data is passed separately to Update() because CoM correction is part
 * of state estimation and intentionally updates the per-tick sensor snapshot.
 */
struct VehicleStateEstimatorInput {
  FilterConfig filter{};
  uint32_t dt_ms{0};
  // Last throttle actually sent to the motor, including slew and trim. ZUPT
  // treats values inside motor_deadzone as non-motion-capable residuals.
  float applied_throttle{0.0f};
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
      SensorSnapshot& sensors, const VehicleStateEstimatorInput& input
#ifdef RC_PROFILE_LOOP
      ,
      const VehicleControlPlatform* profile_clock = nullptr
#endif
      ) noexcept;

  /** Reset state tied to the calibrated vehicle reference frame. */
  void OnReferenceFrameChanged() noexcept;

  /** Refresh only EKF-backed fields after an external EKF reset. */
  void RefreshEkfFields(VehicleStateEstimate& estimate) const noexcept;

#ifdef RC_PROFILE_LOOP
  [[nodiscard]] const VehicleStateEstimatorProfile& GetProfileStats()
      const noexcept {
    return profile_;
  }
  void ResetProfileStats() noexcept { profile_ = {}; }
#endif

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
#ifdef RC_PROFILE_LOOP
  VehicleStateEstimatorProfile profile_;
#endif
};

}  // namespace rc_vehicle
