#include "auto_drive_coordinator.hpp"

#include "calibration_manager.hpp"

namespace rc_vehicle {

bool AutoDriveCoordinator::IsAnyActive() const {
  if (calib_mgr_ && calib_mgr_->IsAutoForwardActive()) return true;
  if (trim_calib_.IsActive()) return true;
  if (com_calib_.IsActive()) return true;
  if (test_runner_.IsActive()) return true;
  if (speed_calib_.IsActive()) return true;
  return false;
}

AutoDriveOutput AutoDriveCoordinator::Update(const AutoDriveInput& input) {
  AutoDriveOutput out;

  // Auto-forward calibration (управляется CalibrationManager)
  if (calib_mgr_ && calib_mgr_->IsAutoForwardActive() && !input.rc_active) {
    out.active = true;
    out.throttle = calib_mgr_->UpdateAutoForward(
        input.fwd_accel, input.accel_mag, input.gyro_z, input.dt_sec);
    out.steering = 0.0f;
    return out;
  }

  // Steering trim calibration
  if (trim_calib_.IsActive() && !input.rc_active) {
    out.active = true;
    trim_calib_.Update(input.fwd_accel, input.accel_mag, input.gyro_z,
                       input.dt_sec, out.throttle, out.steering);
    if (trim_calib_.IsFinished()) {
      out.trim_completed = true;
      out.trim_result = trim_calib_.GetResult();
    }
    return out;
  }

  // Test runner
  if (test_runner_.IsActive() && !input.rc_active) {
    out.active = true;
    test_runner_.Update(input.fwd_accel, input.accel_mag, input.gyro_z,
                        input.dt_sec, out.throttle, out.steering);
    return out;
  }

  // CoM offset calibration
  if (com_calib_.IsActive() && !input.rc_active) {
    out.active = true;
    com_calib_.Update(input.fwd_accel, input.accel_mag, input.cal_ax,
                      input.cal_ay, input.gyro_z, input.dt_sec, out.throttle,
                      out.steering);
    if (com_calib_.IsFinished()) {
      out.com_completed = true;
      out.com_result = com_calib_.GetResult();
    }
    return out;
  }

  // Speed calibration
  if (speed_calib_.IsActive() && !input.rc_active) {
    out.active = true;
    speed_calib_.Update(input.speed_ms, input.accel_mag, input.dt_sec,
                        out.throttle, out.steering);
    if (speed_calib_.IsFinished()) {
      out.speed_cal_completed = true;
      out.speed_cal_result = speed_calib_.GetResult();
    }
    return out;
  }

  return out;
}

bool AutoDriveCoordinator::StartTrimCalib(float target_accel_g,
                                           float current_trim,
                                           float steer_to_yaw_rate_dps) {
  if (IsAnyActive()) return false;
  return trim_calib_.Start(target_accel_g, current_trim, steer_to_yaw_rate_dps);
}

bool AutoDriveCoordinator::StartComCalib(float target_accel_g,
                                          float steering_magnitude,
                                          float cruise_duration_sec,
                                          const float* gravity_vec) {
  if (IsAnyActive()) return false;
  return com_calib_.Start(target_accel_g, steering_magnitude,
                          cruise_duration_sec, gravity_vec);
}

bool AutoDriveCoordinator::StartTest(const TestParams& params) {
  if (IsAnyActive()) return false;
  return test_runner_.Start(params);
}

bool AutoDriveCoordinator::StartSpeedCalib(float target_throttle,
                                            float cruise_duration_sec) {
  if (IsAnyActive()) return false;
  return speed_calib_.Start(target_throttle, cruise_duration_sec);
}

void AutoDriveCoordinator::StopAll() {
  if (calib_mgr_) calib_mgr_->StopAutoForward();
  trim_calib_.Stop();
  com_calib_.Stop();
  test_runner_.Stop();
  speed_calib_.Stop();
}

}  // namespace rc_vehicle
