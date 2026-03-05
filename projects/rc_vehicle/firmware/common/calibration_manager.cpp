#include "calibration_manager.hpp"

#include "vehicle_ekf.hpp"

namespace rc_vehicle {

CalibrationManager::CalibrationManager(VehicleControlPlatform& platform,
                                       ImuCalibration& imu_calib,
                                       MadgwickFilter& madgwick,
                                       VehicleEkf* ekf)
    : platform_(platform),
      imu_calib_(imu_calib),
      madgwick_(madgwick),
      ekf_(ekf) {}

void CalibrationManager::StartCalibration(bool full) {
  calib_request_.store(full ? 2 : 1);
}

bool CalibrationManager::StartForwardCalibration() {
  return imu_calib_.StartForwardCalibration(2000);
}

void CalibrationManager::SetForwardDirection(float fx, float fy, float fz) {
  imu_calib_.SetForwardDirection(fx, fy, fz);
  auto result = platform_.SaveCalib(imu_calib_.GetData());
  if (IsOk(result)) {
    platform_.Log(LogLevel::Info, "Forward direction set and saved to NVS");
  }
}

const char* CalibrationManager::GetStatus() const {
  switch (imu_calib_.GetStatus()) {
    case CalibStatus::Idle:
      return "idle";
    case CalibStatus::Collecting:
      return "collecting";
    case CalibStatus::Done:
      return "done";
    case CalibStatus::Failed:
      return "failed";
  }
  return "unknown";
}

int CalibrationManager::GetStage() const { return imu_calib_.GetCalibStage(); }

void CalibrationManager::ProcessRequest(uint32_t now_ms) {
  (void)now_ms;                          // Unused parameter
  int req = calib_request_.exchange(0);  // Атомарное чтение и сброс
  if (req != 0) {
    CalibMode mode = (req == 2) ? CalibMode::Full : CalibMode::GyroOnly;
    int samples = (req == 2) ? 2000 : 1000;
    imu_calib_.StartCalibration(mode, samples);
    platform_.Log(LogLevel::Info, "Calibration stage 1 started");
  }
}

void CalibrationManager::ProcessCompletion() {
  const CalibStatus status = imu_calib_.GetStatus();
  if (status == prev_calib_status_) {
    return;  // Статус не изменился — ничего не делаем
  }
  prev_calib_status_ = status;

  if (status == CalibStatus::Done) {
    auto result = platform_.SaveCalib(imu_calib_.GetData());
    if (IsOk(result)) {
      platform_.Log(LogLevel::Info, "Calibration done, saved to NVS");
    } else {
      platform_.Log(LogLevel::Warning, "Calibration done, NVS save FAILED");
    }
    // Обновить vehicle frame фильтра Madgwick
    const auto& d = imu_calib_.GetData();
    madgwick_.SetVehicleFrame(d.gravity_vec, d.accel_forward_vec, true);

    // Сбросить EKF, чтобы скорость обнулилась после калибровки
    if (ekf_) {
      ekf_->Reset();
      platform_.Log(LogLevel::Info, "EKF state reset after calibration");
    }
  } else if (status == CalibStatus::Failed) {
    platform_.Log(LogLevel::Warning, "IMU calibration FAILED");
  }
}

bool CalibrationManager::LoadFromNvs() {
  auto calib_data = platform_.LoadCalib();
  if (calib_data) {
    imu_calib_.SetData(*calib_data);
    if (imu_calib_.IsValid()) {
      const auto& d = imu_calib_.GetData();
      madgwick_.SetVehicleFrame(d.gravity_vec, d.accel_forward_vec, true);
    }
    platform_.Log(LogLevel::Info, "IMU calibration loaded from NVS");
    return true;
  } else {
    platform_.Log(LogLevel::Info,
                  "No saved IMU calibration — will auto-calibrate at start");
    return false;
  }
}

void CalibrationManager::StartAutoCalibration() {
  imu_calib_.StartCalibration(CalibMode::Full, 1000);
  platform_.Log(LogLevel::Info,
                "IMU auto-calibration started (Full, 1000 samples)");
}

}  // namespace rc_vehicle