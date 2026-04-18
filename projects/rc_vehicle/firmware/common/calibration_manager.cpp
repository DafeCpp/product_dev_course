#include "calibration_manager.hpp"

#include <algorithm>
#include <cstdio>

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

bool CalibrationManager::StartAutoForwardCalibration(float target_accel_g) {
  if (!imu_calib_.StartForwardCalibration(2000)) {
    platform_.Log(LogLevel::Warning,
                  "Auto-forward calib failed to start (need stage 1 full)");
    return false;
  }
  target_accel_g_ = std::clamp(target_accel_g, 0.02f, 0.3f);

  // PID gains для управления throttle по ускорению (g → throttle [0..0.5])
  PidController::Gains gains;
  gains.kp = 1.0f;           // 0.1g ошибки → +0.1 throttle
  gains.ki = 0.5f;           // интеграл компенсирует постоянную ошибку (батарея)
  gains.kd = 0.05f;          // демпфирование
  gains.max_integral = 0.4f; // anti-windup
  gains.max_output = 0.5f;   // макс throttle
  accel_pid_.SetGains(gains);
  accel_pid_.Reset();

  auto_forward_active_ = true;
  auto_phase_ = AutoPhase::Accelerate;
  phase_elapsed_sec_ = 0.0f;
  cruise_throttle_ = 0.0f;
  char msg[80];
  snprintf(msg, sizeof(msg),
           "Auto-forward calib started (PID, target=%.3f g)", target_accel_g_);
  platform_.Log(LogLevel::Info, msg);
  return true;
}

float CalibrationManager::UpdateAutoForward(float current_accel_g,
                                            float accel_magnitude,
                                            float gyro_z_dps,
                                            float dt_sec) {
  if (!auto_forward_active_ || dt_sec <= 0.0f) {
    return 0.0f;
  }

  phase_elapsed_sec_ += dt_sec;

  switch (auto_phase_) {
    case AutoPhase::Accelerate: {
      float error = target_accel_g_ - current_accel_g;
      float throttle = accel_pid_.Step(error, dt_sec);
      throttle = std::clamp(throttle, 0.0f, 0.5f);
      if (throttle > 0.0f && throttle < kMinEffectiveThrottle) {
        throttle = kMinEffectiveThrottle;
      }

      if (phase_elapsed_sec_ >= kAccelDurationSec) {
        // Переход в круиз: фиксируем текущий throttle
        cruise_throttle_ = throttle;
        auto_phase_ = AutoPhase::Cruise;
        phase_elapsed_sec_ = 0.0f;
        platform_.Log(LogLevel::Info,
                      "Auto-forward: cruise phase (hold throttle)");
      }
      return throttle;
    }

    case AutoPhase::Cruise: {
      if (phase_elapsed_sec_ >= kCruiseDurationSec) {
        auto_phase_ = AutoPhase::Brake;
        phase_elapsed_sec_ = 0.0f;
        accel_pid_.Reset();
        platform_.Log(LogLevel::Info, "Auto-forward: braking");
      }
      // Постоянный газ — поддерживаем скорость
      return cruise_throttle_;
    }

    case AutoPhase::Brake: {
      // Детекция остановки: |a| ≈ 1g и |gyro_z| мал
      bool stopped = (std::abs(accel_magnitude - 1.0f) < kStopAccelThresh) &&
                     (std::abs(gyro_z_dps) < kStopGyroThresh);
      bool timeout = phase_elapsed_sec_ >= kBrakeTimeoutSec;

      if (stopped || timeout) {
        platform_.Log(LogLevel::Info,
                      stopped ? "Auto-forward: stopped (ZUPT)"
                              : "Auto-forward: brake timeout");
        StopAutoForward();
        return 0.0f;
      }
      // Торможение: throttle = 0 (ESC neutral / coast)
      return 0.0f;
    }

    default:
      return 0.0f;
  }
}

void CalibrationManager::StopAutoForward() {
  if (auto_forward_active_) {
    auto_forward_active_ = false;
    auto_phase_ = AutoPhase::Idle;
    accel_pid_.Reset();
    platform_.Log(LogLevel::Info, "Auto-forward calibration stopped");
  }
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

  // Авто-движение завершается вместе с калибровкой
  if (status == CalibStatus::Done || status == CalibStatus::Failed) {
    StopAutoForward();
  }

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