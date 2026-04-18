#include "telemetry_builder.hpp"

namespace rc_vehicle {

TelemetrySnapshot BuildTelemetrySnapshot(
    const TelemetryContext& ctx, uint32_t now, const SensorSnapshot& sensors,
    const StabilizationConfig& stab_cfg, DriveMode drive_mode,
    float applied_throttle, float applied_steering, float commanded_throttle,
    float commanded_steering) {
  TelemetrySnapshot snap;
  snap.uptime_ms = now;
  snap.rc_ok = sensors.rc_active;
  snap.wifi_ok = sensors.wifi_active;
  snap.throttle = applied_throttle;
  snap.steering = applied_steering;

  if (sensors.rc_active && sensors.rc_cmd) {
    snap.rc_throttle = sensors.rc_cmd->throttle;
    snap.rc_steering = sensors.rc_cmd->steering;
  }

  snap.cmd_throttle = commanded_throttle;
  snap.cmd_steering = commanded_steering;

  snap.kids_mode_active = (drive_mode == DriveMode::Kids);
  snap.kids_anti_spin_active = ctx.kids_processor.IsAntiSpinActive();
  snap.kids_throttle_limit = stab_cfg.kids_mode.throttle_limit;

  if (sensors.mag_enabled) {
    snap.mag_enabled = true;
    snap.mag_data = sensors.mag_data;
    snap.heading_deg = sensors.heading_deg;
    snap.heading_rel_deg = sensors.heading_rel_deg;
  }

  if (sensors.imu_enabled) {
    snap.imu_enabled = true;
    snap.imu_data = sensors.imu_data;
    snap.filtered_gz = sensors.filtered_gz;
    snap.forward_accel = ctx.imu_calib.GetForwardAccel(sensors.imu_data);
    ctx.madgwick.GetEulerDeg(snap.pitch_deg, snap.roll_deg, snap.yaw_deg);
    snap.calib_status = ctx.imu_calib.GetStatus();
    snap.calib_stage = ctx.imu_calib.GetCalibStage();
    snap.calib_valid = ctx.imu_calib.IsValid();
    if (snap.calib_valid) {
      snap.calib_data = ctx.imu_calib.GetData();
    }
    snap.ekf_available = true;
    snap.ekf_vx = ctx.ekf.GetVx();
    snap.ekf_vy = ctx.ekf.GetVy();
    snap.ekf_yaw_rate = ctx.ekf.GetYawRate();
    snap.ekf_slip_deg = ctx.ekf.GetSlipAngleDeg();
    snap.ekf_speed_ms = ctx.ekf.GetSpeedMs();
    snap.ekf_vx_var = ctx.ekf.GetVxVariance();
    snap.ekf_vy_var = ctx.ekf.GetVyVariance();
    snap.ekf_r_var = ctx.ekf.GetRVariance();
    snap.oversteer_available = true;
    snap.oversteer_active = ctx.oversteer_guard.IsActive();
  }
  return snap;
}

TelemetryLogFrame BuildLogFrame(const TelemetryContext& ctx, uint32_t now,
                                const SensorSnapshot& sensors,
                                float applied_throttle, float applied_steering,
                                float commanded_throttle,
                                float commanded_steering) {
  TelemetryLogFrame frame;
  frame.ts_ms = now;
  frame.ax = sensors.imu_data.ax;
  frame.ay = sensors.imu_data.ay;
  frame.az = sensors.imu_data.az;
  frame.gx = sensors.imu_data.gx;
  frame.gy = sensors.imu_data.gy;
  frame.gz = sensors.imu_data.gz;
  frame.vx = ctx.ekf.GetVx();
  frame.vy = ctx.ekf.GetVy();
  frame.slip_deg = ctx.ekf.GetSlipAngleDeg();
  frame.speed_ms = ctx.ekf.GetSpeedMs();
  frame.throttle = applied_throttle;
  frame.steering = applied_steering;
  ctx.madgwick.GetEulerDeg(frame.pitch_deg, frame.roll_deg, frame.yaw_deg);
  frame.yaw_rate_dps = sensors.filtered_gz;
  frame.oversteer_active = ctx.oversteer_guard.IsActive() ? 1.0f : 0.0f;
  if (sensors.rc_active && sensors.rc_cmd) {
    frame.rc_throttle = sensors.rc_cmd->throttle;
    frame.rc_steering = sensors.rc_cmd->steering;
  }
  frame.cmd_throttle = commanded_throttle;
  frame.cmd_steering = commanded_steering;
  frame.ekf_vx_var = ctx.ekf.GetVxVariance();
  frame.ekf_vy_var = ctx.ekf.GetVyVariance();
  frame.ekf_r_var = ctx.ekf.GetRVariance();
  if (sensors.mag_enabled) {
    frame.mx = sensors.mag_data.mx;
    frame.my = sensors.mag_data.my;
    frame.mz = sensors.mag_data.mz;
    frame.heading_deg = sensors.heading_deg;
    frame.heading_rel_deg = sensors.heading_rel_deg;
  }
  frame.test_marker = ctx.auto_drive.GetTestMarker();
  return frame;
}

}  // namespace rc_vehicle
