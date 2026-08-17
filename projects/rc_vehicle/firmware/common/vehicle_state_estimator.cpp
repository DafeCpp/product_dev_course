#include "vehicle_state_estimator.hpp"

#include <cmath>

#include "control_loop_helpers.hpp"
#ifdef RC_PROFILE_LOOP
#include "vehicle_control_platform.hpp"
#endif

namespace rc_vehicle {

namespace {
constexpr float kG = 9.80665f;
constexpr float kDegToRad = 3.14159265358979f / 180.0f;
constexpr float kNhcMaxYawRateRps = 1.0f;
}  // namespace

VehicleStateEstimate VehicleStateEstimator::Update(
    SensorSnapshot& sensors, const VehicleStateEstimatorInput& input
#ifdef RC_PROFILE_LOOP
    ,
    const VehicleControlPlatform* profile_clock
#endif
    ) noexcept {
#ifdef RC_PROFILE_LOOP
  const auto profile_call = [profile_clock](
                                VehicleStateEstimatorStageProfile& stage,
                                auto&& operation) noexcept {
    if (!profile_clock) {
      operation();
      return;
    }
    const uint64_t start_us = profile_clock->GetTimeUs();
    operation();
    stage.Record(profile_clock->GetTimeUs() - start_us);
  };
  profile_call(profile_.com_offset, [&] {
#endif
    prev_gz_rad_s_ = CorrectImuForComOffset(sensors, imu_calib_, prev_gz_rad_s_,
                                            input.dt_ms);
#ifdef RC_PROFILE_LOOP
  });
#endif

  const bool ekf_active = input.ekf_available && input.filter.ekf_enabled;
  const bool imu_tick_valid = sensors.imu_enabled && input.dt_ms > 0;
  const float dt_sec =
      imu_tick_valid ? static_cast<float>(input.dt_ms) * 0.001f : 0.0f;

  ImuData vehicle_imu = sensors.imu_data;
  if (sensors.imu_enabled) {
#ifdef RC_PROFILE_LOOP
    profile_call(profile_.rotate,
                 [&] { imu_calib_.RotateToVehicleFrame(vehicle_imu); });
#else
    imu_calib_.RotateToVehicleFrame(vehicle_imu);
#endif
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
#ifdef RC_PROFILE_LOOP
    profile_call(profile_.tilt, [&] {
      tilt_est_.Update(vehicle_imu, a_lin_g, a_lin_lat_g, dt_sec);
    });
#else
    tilt_est_.Update(vehicle_imu, a_lin_g, a_lin_lat_g, dt_sec);
#endif
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

    // Match the motor model's definition of motion-capable output. The caller
    // supplies the last value actually sent to the motor, including slew and
    // trim, so ZUPT cannot engage while a stale higher PWM is still applied.
    const float applied_throttle_abs = std::abs(input.applied_throttle);
    const float zupt_throttle_abs =
        applied_throttle_abs > input.filter.motor_deadzone
            ? applied_throttle_abs
            : 0.0f;
#ifdef RC_PROFILE_LOOP
    profile_call(profile_.imu, [&] {
#endif
      ekf_.UpdateFromImu(vehicle_imu.ax, vehicle_imu.ay, vehicle_imu.az,
                         sensors.filtered_gz, dt_sec, zupt_throttle_abs,
                         pitch_rad, roll_rad);
#ifdef RC_PROFILE_LOOP
    });
#endif

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
#ifdef RC_PROFILE_LOOP
      profile_call(profile_.speed, [&] {
#endif
        ekf_.UpdateSpeed(expected_speed, input.filter.speed_meas_noise);
#ifdef RC_PROFILE_LOOP
      });
#endif
    }

    if (input.filter.nhc_enabled &&
        std::abs(ekf_.GetYawRate()) < kNhcMaxYawRateRps) {
#ifdef RC_PROFILE_LOOP
      profile_call(profile_.nhc, [&] {
#endif
        ekf_.UpdateNonHolonomic(input.filter.nhc_noise);
#ifdef RC_PROFILE_LOOP
      });
#endif
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
#ifdef RC_PROFILE_LOOP
    profile_call(profile_.heading, [&] {
#endif
      ekf_.UpdateHeading(sensors.heading_deg * kDegToRad);
#ifdef RC_PROFILE_LOOP
    });
#endif
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
