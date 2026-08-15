#include "vehicle_state_estimator.hpp"

#include <cmath>

#include "control_loop_helpers.hpp"

namespace rc_vehicle {

namespace {
constexpr float kG = 9.80665f;
constexpr float kDegToRad = 3.14159265358979f / 180.0f;
constexpr float kNhcMaxYawRateRps = 1.0f;
}  // namespace

VehicleStateEstimate VehicleStateEstimator::Update(
    SensorSnapshot& sensors, const VehicleStateEstimatorInput& input) noexcept {
  prev_gz_rad_s_ =
      CorrectImuForComOffset(sensors, imu_calib_, prev_gz_rad_s_, input.dt_ms);

  const bool ekf_active = input.ekf_available && input.filter.ekf_enabled;
  const bool imu_tick_valid = sensors.imu_enabled && input.dt_ms > 0;
  const float dt_sec =
      imu_tick_valid ? static_cast<float>(input.dt_ms) * 0.001f : 0.0f;

  ImuData vehicle_imu = sensors.imu_data;
  if (sensors.imu_enabled) {
    imu_calib_.RotateToVehicleFrame(vehicle_imu);
  }

  bool tilt_active_this_tick = false;
  float pitch_rad = 0.0f;
  float roll_rad = 0.0f;
  if (imu_tick_valid && input.filter.tilt_comp_enabled) {
    tilt_active_this_tick = true;
    if (!tilt_was_enabled_) {
      tilt_est_.Reset();
    }

    const bool motor_model_anchor_active = ekf_active &&
                                           input.filter.motor_model_enabled &&
                                           !input.speed_calibration_active;
    const float a_lin_g = motor_model_anchor_active ? a_lin_prev_g_ : 0.0f;
    const float a_lin_lat_g =
        ekf_active ? (vehicle_imu.gz * kDegToRad) * prev_vx_ / kG : 0.0f;

    tilt_est_.SetParams(
        {input.filter.tilt_corr_gain_hz, input.filter.tilt_accel_gate_band_g});
    tilt_est_.Update(vehicle_imu, a_lin_g, a_lin_lat_g, dt_sec);
    pitch_rad = tilt_est_.GetPitchRad();
    roll_rad = tilt_est_.GetRollRad();
  }
  tilt_was_enabled_ = tilt_active_this_tick;

  if (ekf_active && imu_tick_valid) {
    const bool motor_model_active =
        input.filter.motor_model_enabled && !input.speed_calibration_active;

    if (!input.filter.tilt_comp_enabled && input.filter.madgwick_enabled) {
      float yaw_rad = 0.0f;
      madgwick_.GetEulerRad(pitch_rad, roll_rad, yaw_rad);
    }

    // Stabilization can leave a small non-zero throttle while stationary.
    // Match the motor model's definition of motion-capable command so those
    // sub-deadzone residuals do not suppress ZUPT indefinitely.
    const float commanded_throttle_abs = std::abs(input.commanded_throttle);
    const float zupt_throttle_abs =
        commanded_throttle_abs > input.filter.motor_deadzone
            ? commanded_throttle_abs
            : 0.0f;
    ekf_.UpdateFromImu(vehicle_imu.ax, vehicle_imu.ay, vehicle_imu.az,
                       sensors.filtered_gz, dt_sec, zupt_throttle_abs,
                       pitch_rad, roll_rad);

    if (motor_model_active) {
      const float throttle = input.motor_model_throttle;
      const float throttle_abs = std::abs(throttle);
      float expected_speed = 0.0f;
      if (throttle_abs > input.filter.motor_deadzone &&
          input.filter.motor_deadzone < 1.0f) {
        const float sign = throttle < 0.0f ? -1.0f : 1.0f;
        expected_speed = sign * input.filter.motor_speed_gain *
                         (throttle_abs - input.filter.motor_deadzone) /
                         (1.0f - input.filter.motor_deadzone);
      }
      ekf_.UpdateSpeed(expected_speed, input.filter.speed_meas_noise);
    }

    if (input.filter.nhc_enabled &&
        std::abs(ekf_.GetYawRate()) < kNhcMaxYawRateRps) {
      ekf_.UpdateNonHolonomic(input.filter.nhc_noise);
    }

    const float vx_now = ekf_.GetVx();
    a_lin_prev_g_ = (vx_now - prev_vx_) / dt_sec / kG;
    prev_vx_ = vx_now;
  }

  float forward_accel_g = 0.0f;
  if (sensors.imu_enabled) {
    forward_accel_g =
        ComputeForwardAccelG(imu_calib_, vehicle_imu, sensors.imu_data,
                             tilt_est_.GetPitchRad(), tilt_active_this_tick);
  }

  if (ekf_active && sensors.imu_enabled && sensors.mag_enabled &&
      !sensors.mag_rejected &&
      sensors.mag_sample_sequence != last_mag_sample_sequence_) {
    ekf_.UpdateHeading(sensors.heading_deg * kDegToRad);
    last_mag_sample_sequence_ = sensors.mag_sample_sequence;
  }

  return BuildEstimate(sensors.imu_enabled, tilt_active_this_tick, pitch_rad,
                       roll_rad, forward_accel_g);
}

void VehicleStateEstimator::OnReferenceFrameChanged() noexcept {
  tilt_est_.Reset();
  prev_vx_ = ekf_.GetVx();
  a_lin_prev_g_ = 0.0f;
  last_mag_sample_sequence_ = 0;
  tilt_was_enabled_ = false;
}

void VehicleStateEstimator::RefreshEkfFields(
    VehicleStateEstimate& estimate) const noexcept {
  estimate.yaw_rad = ekf_.GetYawRad();
  estimate.vx_ms = ekf_.GetVx();
  estimate.vy_ms = ekf_.GetVy();
  estimate.speed_ms = ekf_.GetSpeedMs();
  estimate.yaw_rate_rps = ekf_.GetYawRate();
  estimate.slip_angle_rad = ekf_.GetSlipAngleRad();
  estimate.zupt_status = ekf_.GetZuptStatus();
  estimate.ekf_diverged = ekf_.IsDiverged();
}

VehicleStateEstimate VehicleStateEstimator::BuildEstimate(
    bool imu_valid, bool tilt_valid, float pitch_rad, float roll_rad,
    float forward_accel_g) const noexcept {
  VehicleStateEstimate estimate;
  estimate.imu_valid = imu_valid;
  estimate.tilt_valid = tilt_valid;
  estimate.pitch_rad = pitch_rad;
  estimate.roll_rad = roll_rad;
  estimate.forward_accel_g = forward_accel_g;
  RefreshEkfFields(estimate);
  return estimate;
}

}  // namespace rc_vehicle
