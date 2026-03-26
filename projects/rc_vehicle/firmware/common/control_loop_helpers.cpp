#include "control_loop_helpers.hpp"

namespace rc_vehicle {

void HandleAutoDriveCompletion(const AutoDriveOutput& ad_out,
                               StabilizationManager* stab_mgr,
                               ImuCalibration& imu_calib,
                               VehicleControlPlatform& platform) {
  if (ad_out.trim_completed) {
    if (ad_out.trim_result.valid && stab_mgr) {
      auto cfg = stab_mgr->GetConfig();
      cfg.steering_trim = ad_out.trim_result.trim;
      stab_mgr->SetConfig(cfg, true);
      platform.Log(LogLevel::Info, "Steering trim calibration done");
    } else if (!ad_out.trim_result.valid) {
      platform.Log(LogLevel::Warning, "Steering trim calibration failed");
    }
  }

  if (ad_out.com_completed) {
    if (ad_out.com_result.valid) {
      auto data = imu_calib.GetData();
      data.com_offset[0] = ad_out.com_result.rx;
      data.com_offset[1] = ad_out.com_result.ry;
      imu_calib.SetData(data);
      platform.SaveComOffset(data.com_offset);
      platform.Log(LogLevel::Info, "CoM offset calibration done");
    } else {
      platform.Log(LogLevel::Warning, "CoM offset calibration failed");
    }
  }

  if (ad_out.speed_cal_completed) {
    if (ad_out.speed_cal_result.valid) {
      platform.Log(LogLevel::Info, "Speed calibration done");
    } else {
      platform.Log(LogLevel::Warning, "Speed calibration failed");
    }
  }
}

SelfTestInput BuildSelfTestInput(const SelfTestContext& ctx) {
  SelfTestInput input;

  input.loop_hz = ctx.last_loop_hz.load(std::memory_order_relaxed);

  if (ctx.imu_handler) {
    input.imu_enabled = ctx.imu_handler->IsEnabled();
    const auto& imu = ctx.imu_handler->GetData();
    input.gyro_x_dps = imu.gx;
    input.gyro_y_dps = imu.gy;
    input.gyro_z_dps = imu.gz;
    input.accel_x_g = imu.ax;
    input.accel_y_g = imu.ay;
    input.accel_z_g = imu.az;
  }

  {
    float pitch = 0, roll = 0, yaw = 0;
    ctx.madgwick.GetEulerDeg(pitch, roll, yaw);
    input.pitch_deg = pitch;
    input.roll_deg = roll;
  }

  input.ekf_vx = ctx.ekf.GetVx();
  input.ekf_vy = ctx.ekf.GetVy();

  bool rc_ok = ctx.rc_handler && ctx.rc_handler->IsActive();
  bool wifi_ok = ctx.wifi_handler && ctx.wifi_handler->IsActive();
  input.failsafe_active = !rc_ok && !wifi_ok;

  input.calib_valid = ctx.imu_calib.IsValid();

  if (ctx.telem_mgr) {
    size_t count = 0, cap = 0;
    ctx.telem_mgr->GetLogInfo(count, cap);
    input.log_capacity = cap;
  }

  input.pwm_status = (ctx.platform_exists && ctx.inited) ? 0 : -1;

  return input;
}

}  // namespace rc_vehicle
