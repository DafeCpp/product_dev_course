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

  MotionDriver::Config cfg;
  cfg.accel_mode = MotionDriver::AccelMode::Pid;
  cfg.pid_gains = {0.3f, 0.2f, 0.0f, 0.15f, 0.5f};
  cfg.target_value = std::clamp(target_accel_g, 0.02f, 0.3f);
  cfg.accel_duration_sec = 1.5f;
  cfg.min_effective_throttle = 0.0f;
  cfg.brake_throttle = 0.0f;
  cfg.brake_timeout_sec = 3.0f;
  cfg.zupt = {0.05f, 3.0f};
  cfg.breakaway = {0.5f, 0.25f, 0.03f, 25};
  driver_.Start(cfg);

  char msg[80];
  snprintf(msg, sizeof(msg),
           "Auto-forward calib started (PID, target=%.3f g)", cfg.target_value);
  platform_.Log(LogLevel::Info, msg);
  if (event_log_) {
    // param: 2 = auto_forward (stage 2)
    event_log_->Push({platform_.GetTimeMs(),
                      TelemetryEventType::ImuCalibStart,
                      2,
                      {},
                      cfg.target_value,
                      0.0f});
  }
  return true;
}

float CalibrationManager::UpdateAutoForward(float current_accel_g,
                                            float accel_magnitude,
                                            float gyro_z_dps,
                                            float dt_sec) {
  if (!IsAutoForwardActive()) {
    return 0.0f;
  }

  float throttle =
      driver_.Update(current_accel_g, accel_magnitude, gyro_z_dps, dt_sec);

  MotionPhase phase = driver_.GetPhase();

  if (phase == MotionPhase::Cruise) {
    if (driver_.GetPhaseElapsed() == 0.0f) {
      // Just transitioned into Cruise
      platform_.Log(LogLevel::Info, "Auto-forward: cruise phase (hold throttle)");
    }
    if (driver_.GetPhaseElapsed() >= kCruiseDurationSec) {
      driver_.EndCruise();
      platform_.Log(LogLevel::Info, "Auto-forward: braking");
    }
  }

  if (phase == MotionPhase::Stopped) {
    platform_.Log(LogLevel::Info, "Auto-forward: stopped (ZUPT)");
    driver_.Reset();
  }

  return throttle;
}

void CalibrationManager::StopAutoForward() {
  if (IsAutoForwardActive()) {
    driver_.Reset();
    // Сбор семплов обязан прекратиться вместе с движением: иначе этап 2
    // доберёт остаток уже без управляемого разгона (под RC или на стоящей
    // машине) и запишет в NVS мусорную ось «вперёд» — ровно тот сценарий,
    // который и привёл к LOS-214.
    //
    // Прерванный сбор логируем: до LOS-226 обрыв auto-forward не оставлял в
    // логе никакого следа. Условие — сбор реально шёл: ProcessCompletion()
    // зовёт нас и при штатном завершении (Done/Failed), и тогда событие об
    // обрыве было бы ложным.
    const bool was_collecting =
        imu_calib_.GetStatus() == CalibStatus::Collecting;
    imu_calib_.CancelCalibration();
    if (was_collecting && event_log_) {
      // param: 2 = stage 2 (auto_forward), как у ImuCalibStart
      event_log_->Push(
          {platform_.GetTimeMs(), TelemetryEventType::ImuCalibFailed, 2});
      // Об этом обрыве уже сообщили. ProcessCompletion() выполняется раньше
      // UpdateAutoDrive() в тике, поэтому переход Collecting → Failed он
      // увидит только на следующем и записал бы второе ImuCalibFailed — да
      // ещё с param = 0, потому что после отмены GetCalibStage() не помнит
      // стадию. Синхронизируем, чтобы дубля не было.
      prev_calib_status_ = imu_calib_.GetStatus();
    }
    platform_.Log(LogLevel::Info, "Auto-forward calibration stopped");
  }
}

void CalibrationManager::SetForwardDirection(float fx, float fy, float fz) {
  // Вызывается с потока WS/HTTP-сервера — НЕ трогаем imu_calib_/madgwick_
  // отсюда напрямую (ни один не потокобезопасен, оба непрерывно
  // используются control loop на 500 Гц); только откладываем запрос под
  // мьютексом, как StartCalibration()/calib_request_ (код-ревью PR #290,
  // 7-й раунд). Применяется в ProcessForwardDirectionRequest().
  std::lock_guard<std::mutex> lock(forward_dir_mutex_);
  forward_dir_fx_ = fx;
  forward_dir_fy_ = fy;
  forward_dir_fz_ = fz;
  forward_dir_pending_ = true;
}

void CalibrationManager::ProcessForwardDirectionRequest() {
  float fx, fy, fz;
  {
    std::lock_guard<std::mutex> lock(forward_dir_mutex_);
    if (!forward_dir_pending_) return;
    forward_dir_pending_ = false;
    fx = forward_dir_fx_;
    fy = forward_dir_fy_;
    fz = forward_dir_fz_;
  }

  imu_calib_.SetForwardDirection(fx, fy, fz);

  // Как и на завершении Full/Forward в ProcessCompletion() (код-ревью
  // PR #290, 6-й раунд): это тоже меняет базис RotateToVehicleFrame(), и
  // Madgwick, и TiltEstimator/prev_vx_/a_lin_prev_g_ вызывающего кода
  // должны обновиться/сброситься — иначе оба используют устаревшую СК
  // до следующей полной калибровки.
  const auto& d = imu_calib_.GetData();
  madgwick_.SetVehicleFrame(d.gravity_vec, d.accel_forward_vec, true);
  frame_changed_ = true;

  // Сбросить EKF — симметрично ProcessCompletion() (код-ревью PR #290,
  // 10-й раунд): vx/vy персистентны и выражены в СТАРЫХ осях forward/
  // lateral; смена базиса здесь меняет, что означают эти оси, но НЕ сами
  // значения состояния — без сброса следующий тик интерпретировал бы
  // старые координаты скорости в новой СК, портя speed/slip и motor/NHC
  // апдейты после ручной смены направления «вперёд» посреди сессии.
  if (ekf_) {
    ekf_->Reset();
    platform_.Log(LogLevel::Info,
                  "EKF state reset after forward direction change");
  }

  auto result = platform_.SaveCalib(imu_calib_.GetData());
  if (result.has_value()) {
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
  int req = calib_request_.exchange(0);  // Атомарное чтение и сброс
  if (req != 0) {
    CalibMode mode = (req == 2) ? CalibMode::Full : CalibMode::GyroOnly;
    int samples = (req == 2) ? 2000 : 1000;
    imu_calib_.StartCalibration(mode, samples);
    platform_.Log(LogLevel::Info, "Calibration stage 1 started");
    if (event_log_) {
      // param: 0 = gyro_only, 1 = full
      event_log_->Push({now_ms, TelemetryEventType::ImuCalibStart,
                        static_cast<uint8_t>(req == 2 ? 1 : 0)});
    }
  }
}

void CalibrationManager::ProcessCompletion(uint32_t now_ms) {
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
    if (result.has_value()) {
      platform_.Log(LogLevel::Info, "Calibration done, saved to NVS");
    } else {
      platform_.Log(LogLevel::Warning, "Calibration done, NVS save FAILED");
    }
    // Обновить vehicle frame фильтра Madgwick
    const auto& d = imu_calib_.GetData();
    madgwick_.SetVehicleFrame(d.gravity_vec, d.accel_forward_vec, true);
    frame_changed_ = true;  // см. ConsumeFrameChanged()

    // Сбросить EKF, чтобы скорость обнулилась после калибровки
    if (ekf_) {
      ekf_->Reset();
      platform_.Log(LogLevel::Info, "EKF state reset after calibration");
    }
    if (event_log_) {
      uint8_t stage = static_cast<uint8_t>(imu_calib_.GetCalibStage());
      event_log_->Push({now_ms, TelemetryEventType::ImuCalibDone, stage});
    }
  } else if (status == CalibStatus::Failed) {
    platform_.Log(LogLevel::Warning, "IMU calibration FAILED");
    if (event_log_) {
      uint8_t stage = static_cast<uint8_t>(imu_calib_.GetCalibStage());
      event_log_->Push({now_ms, TelemetryEventType::ImuCalibFailed, stage});
    }
  }
}

bool CalibrationManager::LoadFromNvs() {
  auto calib_data = platform_.LoadCalib();
  if (calib_data) {
    imu_calib_.SetData(*calib_data);
    if (imu_calib_.IsValid()) {
      const auto& d = imu_calib_.GetData();
      madgwick_.SetVehicleFrame(d.gravity_vec, d.accel_forward_vec, true);
      if (!d.forward_valid) {
        // Сохранённая ось «вперёд» не горизонтальна — отброшена в пользу оси X.
        // Авто-манёвры и калибровки поедут по неверной оси (LOS-214).
        platform_.Log(LogLevel::Warning,
                      "Saved forward axis rejected (not horizontal) — "
                      "run Full + Forward calibration");
      }
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