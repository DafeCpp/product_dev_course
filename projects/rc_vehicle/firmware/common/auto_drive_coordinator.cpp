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
      if (event_log_) {
        TelemetryEventType t = out.trim_result.valid
                                   ? TelemetryEventType::TrimCalibDone
                                   : TelemetryEventType::TrimCalibFailed;
        event_log_->Push({input.ts_ms, t, 0});
      }
    }
    return out;
  }

  // Test runner
  if (test_runner_.IsActive() && !input.rc_active) {
    out.active = true;
    test_runner_.Update(input.fwd_accel, input.accel_mag, input.gyro_z,
                        input.dt_sec, out.throttle, out.steering);
    if (test_runner_.IsFinished() && event_log_) {
      auto status = test_runner_.GetStatus();
      TelemetryEventType t = (status.phase == TestRunner::Phase::Done)
                                 ? TelemetryEventType::TestDone
                                 : TelemetryEventType::TestFailed;
      event_log_->Push({input.ts_ms, t,
                        static_cast<uint8_t>(status.type)});
    }
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
      if (event_log_) {
        TelemetryEventType t = out.com_result.valid
                                   ? TelemetryEventType::ComCalibDone
                                   : TelemetryEventType::ComCalibFailed;
        event_log_->Push({input.ts_ms, t, 0});
      }
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
      if (event_log_) {
        TelemetryEventType t = out.speed_cal_result.valid
                                   ? TelemetryEventType::SpeedCalibDone
                                   : TelemetryEventType::SpeedCalibFailed;
        event_log_->Push({input.ts_ms, t, 0});
      }
    }
    return out;
  }

  return out;
}

bool AutoDriveCoordinator::StartTrimCalib(float target_accel_g,
                                           float current_trim,
                                           float steer_to_yaw_rate_dps) {
  if (IsAnyActive()) return false;
  if (!trim_calib_.Start(target_accel_g, current_trim, steer_to_yaw_rate_dps)) {
    return false;
  }
  if (event_log_) {
    event_log_->Push({0, TelemetryEventType::TrimCalibStart, 0, {},
                      target_accel_g, 0.0f});
  }
  return true;
}

bool AutoDriveCoordinator::StartComCalib(float target_accel_g,
                                          float steering_magnitude,
                                          float cruise_duration_sec,
                                          const float* gravity_vec) {
  if (IsAnyActive()) return false;
  if (!com_calib_.Start(target_accel_g, steering_magnitude,
                        cruise_duration_sec, gravity_vec)) {
    return false;
  }
  if (event_log_) {
    event_log_->Push({0, TelemetryEventType::ComCalibStart, 0, {},
                      target_accel_g, steering_magnitude});
  }
  return true;
}

bool AutoDriveCoordinator::StartTest(const TestParams& params) {
  if (IsAnyActive()) return false;
  if (!test_runner_.Start(params)) return false;
  if (event_log_) {
    event_log_->Push({0, TelemetryEventType::TestStart,
                      static_cast<uint8_t>(params.type), {},
                      params.duration_sec, params.steering});
  }
  return true;
}

bool AutoDriveCoordinator::StartSpeedCalib(float target_throttle,
                                            float cruise_duration_sec) {
  if (IsAnyActive()) return false;
  if (!speed_calib_.Start(target_throttle, cruise_duration_sec)) return false;
  if (event_log_) {
    event_log_->Push({0, TelemetryEventType::SpeedCalibStart, 0, {},
                      target_throttle, cruise_duration_sec});
  }
  return true;
}

void AutoDriveCoordinator::StopAll() {
  // Остановка из failsafe: логируем только активные процедуры
  if (event_log_) {
    if (trim_calib_.IsActive()) {
      event_log_->Push({0, TelemetryEventType::TrimCalibFailed, 0});
    }
    if (com_calib_.IsActive()) {
      event_log_->Push({0, TelemetryEventType::ComCalibFailed, 0});
    }
    if (test_runner_.IsActive()) {
      auto status = test_runner_.GetStatus();
      event_log_->Push({0, TelemetryEventType::TestStopped,
                        static_cast<uint8_t>(status.type)});
    }
    if (speed_calib_.IsActive()) {
      event_log_->Push({0, TelemetryEventType::SpeedCalibFailed, 0});
    }
  }
  if (calib_mgr_) calib_mgr_->StopAutoForward();
  trim_calib_.Stop();
  com_calib_.Stop();
  test_runner_.Stop();
  speed_calib_.Stop();
}

}  // namespace rc_vehicle
