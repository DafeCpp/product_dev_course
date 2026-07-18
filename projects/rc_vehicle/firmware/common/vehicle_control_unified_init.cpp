#include "vehicle_control_unified.hpp"

#include <iomanip>

#include "calibration_manager.hpp"
#include "config.hpp"
#include "control_components.hpp"
#include "log_format.hpp"
#include "rc_vehicle_common.hpp"
#include "stabilization_manager.hpp"
#include "telemetry_manager.hpp"

namespace rc_vehicle {

PlatformError VehicleControlUnified::Init() {
  if (inited_) return PlatformError::Ok;
  if (!platform_) return PlatformError::TaskCreateFailed;

  auto pwm_result = platform_->InitPwm();
  if (!pwm_result.has_value()) {
    platform_->Log(LogLevel::Error, "Failed to initialize PWM");
    return pwm_result.error();
  }

  auto failsafe_result = platform_->InitFailsafe();
  if (!failsafe_result.has_value()) {
    platform_->Log(LogLevel::Error, "Failed to initialize failsafe");
    return failsafe_result.error();
  }

  rc_enabled_ = platform_->InitRc().has_value();
  if (!rc_enabled_) {
    platform_->Log(LogLevel::Warning,
                   "RC input init failed — continuing without RC-in");
  }

  if (platform_->InitMag()) {
    platform_->Log(LogLevel::Info, "Magnetometer initialized");
  } else {
    platform_->Log(LogLevel::Info, "Magnetometer not available");
  }

  InitImuSubsystem();
  InitTelemetryLog();

  if (!InitializeComponents()) return PlatformError::TaskCreateFailed;

  auto task_result = platform_->CreateTask(ControlTaskEntry, this);
  if (!task_result.has_value()) {
    platform_->Log(LogLevel::Error, "Failed to create vehicle control task");
    return task_result.error();
  }

  inited_ = true;
  platform_->Log(LogLevel::Info,
                 "Vehicle control started (unified architecture)");
  return PlatformError::Ok;
}

void VehicleControlUnified::InitImuSubsystem() {
  if (!platform_->InitImu().has_value()) {
    imu_enabled_ = false;
    const int who = platform_->GetImuLastWhoAmI();
    platform_->Log(LogLevel::Warning,
                   "IMU init failed — continuing without IMU");
    if (who >= 0) {
      LogFormat fmt;
      fmt << "IMU WHO_AM_I = 0x" << std::hex << std::setw(2)
          << std::setfill('0') << static_cast<unsigned>(who);
      platform_->Log(LogLevel::Info, fmt.str());
    }
    return;
  }

  imu_enabled_ = true;
  calib_mgr_.reset(
      new CalibrationManager(*platform_, imu_calib_, madgwick_, &ekf_));
  stab_mgr_.reset(new StabilizationManager(*platform_, madgwick_, yaw_ctrl_,
                                           slip_ctrl_, nullptr));
  telem_mgr_.reset(new TelemetryManager());

  auto_drive_.SetCalibrationManager(calib_mgr_.get());

  // Провязать лог событий для калибровки и авто-манёвров
  TelemetryEventLog* ev_log = telem_mgr_->GetEventLog();
  calib_mgr_->SetEventLog(ev_log);
  auto_drive_.SetEventLog(ev_log);

  calib_mgr_->LoadFromNvs();

  float com_off[2]{0.f, 0.f};
  if (platform_->LoadComOffset(com_off)) {
    auto data = imu_calib_.GetData();
    data.com_offset[0] = com_off[0];
    data.com_offset[1] = com_off[1];
    imu_calib_.SetData(data);
  }

  stab_mgr_->LoadFromNvs();
  stab_mgr_->ApplyConfig();
  calib_mgr_->StartAutoCalibration();
}

void VehicleControlUnified::InitTelemetryLog() {
  if (!telem_mgr_) {
    platform_->Log(LogLevel::Warning, "TelemetryLog: disabled (no IMU)");
    return;
  }
  if (!telem_mgr_->Init(config::TelemetryLogConfig::kCapacityFrames)) {
    platform_->Log(
        LogLevel::Warning,
        "TelemetryLog: failed to allocate (no PSRAM?), log disabled");
    return;
  }
  LogFormat fmt;
  fmt << "TelemetryLog: allocated "
      << static_cast<unsigned>(config::TelemetryLogConfig::kCapacityFrames)
      << " frames";
  platform_->Log(LogLevel::Info, fmt.str());
}

bool VehicleControlUnified::InitializeComponents() {
  if (rc_enabled_) {
    rc_handler_.reset(
        new RcInputHandler(*platform_, config::RcInputConfig::kPollIntervalMs));
  }
  wifi_handler_.reset(
      new WifiCommandHandler(*platform_, config::WifiConfig::kCommandTimeoutMs));

  if (imu_enabled_) {
    imu_handler_.reset(new ImuHandler(*platform_, imu_calib_, madgwick_,
                                      config::ImuConfig::kReadIntervalMs));
    imu_handler_->SetEnabled(true);
    imu_handler_->SetLpfCutoff(stab_mgr_->GetConfig().filter.lpf_cutoff_hz);
    stab_mgr_.reset(new StabilizationManager(*platform_, madgwick_, yaw_ctrl_,
                                             slip_ctrl_, imu_handler_.get()));
    stab_mgr_->LoadFromNvs();
    stab_mgr_->ApplyConfig();

    // Загрузить калибровку магнитометра из NVS
    MagCalibData mag_calib_data{};
    if (platform_->LoadMagCalib(mag_calib_data)) {
      mag_calib_.SetData(mag_calib_data);
      platform_->Log(LogLevel::Info, "Mag calibration loaded from NVS");
    }
    imu_handler_->SetMagCalibration(&mag_calib_);
  }

  if (!rc_handler_)  rc_handler_.reset(new RcInputHandler(*platform_, 0));
  if (!imu_handler_) imu_handler_.reset(
      new ImuHandler(*platform_, imu_calib_, madgwick_, 0));

  // Без IMU stab_mgr_ не создаётся (InitImuSubsystem выходит раньше) —
  // контроллеры инициализируются дефолтным конфигом, стабилизация неактивна.
  //
  // FW-R23: контроллеры конфиг НЕ хранят — получают живой снимок в Process()
  // каждый тик. `cfg` ниже нужен лишь для начальных PID-коэффициентов
  // (yaw/slip SetGains в Init); указатель на него нигде не сохраняется.
  const StabilizationConfig cfg =
      stab_mgr_ ? stab_mgr_->GetConfig() : StabilizationConfig{};
  yaw_ctrl_.Init(cfg, ekf_, imu_handler_.get());
  pitch_ctrl_.Init(madgwick_, imu_handler_.get());
  slip_ctrl_.Init(cfg, ekf_, imu_handler_.get());
  oversteer_guard_.Init(ekf_, imu_handler_.get());
  kids_processor_.Init(ekf_, imu_handler_.get());

  telem_handler_.reset(new TelemetryHandler(
      *platform_, config::TelemetryConfig::kSendIntervalMs));
  return true;
}

}  // namespace rc_vehicle
