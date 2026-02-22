#include "vehicle_control_unified.hpp"

#include "rc_vehicle_common.hpp"
#include "slew_rate.hpp"

namespace rc_vehicle {

// ═════════════════════════════════════════════════════════════════════════
// Константы (будут перенесены в config_common.hpp позже)
// ═════════════════════════════════════════════════════════════════════════

namespace {
constexpr uint32_t CONTROL_LOOP_PERIOD_MS = 2;   // 500 Hz
constexpr uint32_t PWM_UPDATE_INTERVAL_MS = 20;  // 50 Hz
constexpr uint32_t RC_IN_POLL_INTERVAL_MS = 20;  // 50 Hz
constexpr uint32_t IMU_READ_INTERVAL_MS = 2;     // 500 Hz
constexpr uint32_t TELEM_SEND_INTERVAL_MS = 50;  // 20 Hz
constexpr uint32_t WIFI_CMD_TIMEOUT_MS = 500;

constexpr float SLEW_RATE_THROTTLE_MAX_PER_SEC = 0.5f;
constexpr float SLEW_RATE_STEERING_MAX_PER_SEC = 1.0f;

constexpr uint32_t DIAG_INTERVAL_MS = 5000;  // Диагностика каждые 5 секунд
}  // namespace

// ═════════════════════════════════════════════════════════════════════════
// VehicleControlUnified Implementation
// ═════════════════════════════════════════════════════════════════════════

VehicleControlUnified& VehicleControlUnified::Instance() {
  static VehicleControlUnified s_instance;
  return s_instance;
}

void VehicleControlUnified::SetPlatform(
    std::unique_ptr<VehicleControlPlatform> platform) {
  platform_ = std::move(platform);
}

void VehicleControlUnified::ControlTaskEntry(void* arg) {
  auto* self = static_cast<VehicleControlUnified*>(arg);
  if (self) {
    self->ControlTaskLoop();
  }
}

void VehicleControlUnified::ControlTaskLoop() {
  if (!platform_) {
    return;  // Платформа не установлена
  }

  // Slew rate для плавности управления
  float commanded_throttle = 0.0f;
  float commanded_steering = 0.0f;
  float applied_throttle = 0.0f;
  float applied_steering = 0.0f;

  uint32_t last_pwm_update = platform_->GetTimeMs();
  uint32_t last_loop = platform_->GetTimeMs();

  // Диагностика
  uint32_t diag_loop_count = 0;
  uint32_t diag_start_ms = platform_->GetTimeMs();

  while (true) {
    platform_->DelayUntilNextTick(CONTROL_LOOP_PERIOD_MS);
    const uint32_t now = platform_->GetTimeMs();
    const uint32_t dt_ms = now - last_loop;
    last_loop = now;
    ++diag_loop_count;

    // ─────────────────────────────────────────────────────────────────────
    // Обновление всех компонентов
    // ─────────────────────────────────────────────────────────────────────

    if (rc_handler_) rc_handler_->Update(now, dt_ms);
    if (wifi_handler_) wifi_handler_->Update(now, dt_ms);
    if (imu_handler_) imu_handler_->Update(now, dt_ms);

    // ─────────────────────────────────────────────────────────────────────
    // Обработка запроса калибровки
    // ─────────────────────────────────────────────────────────────────────

    ProcessCalibrationRequest(now);
    ProcessCalibrationCompletion();

    // ─────────────────────────────────────────────────────────────────────
    // Выбор источника управления (RC приоритетнее Wi-Fi)
    // ─────────────────────────────────────────────────────────────────────

    SelectControlSource(commanded_throttle, commanded_steering);

    // ─────────────────────────────────────────────────────────────────────
    // Failsafe
    // ─────────────────────────────────────────────────────────────────────

    bool rc_active = rc_handler_ && rc_handler_->IsActive();
    bool wifi_active = wifi_handler_ && wifi_handler_->IsActive();

    if (platform_->FailsafeUpdate(rc_active, wifi_active)) {
      // Failsafe активен: нейтраль
      commanded_throttle = 0.0f;
      commanded_steering = 0.0f;
      applied_throttle = 0.0f;
      applied_steering = 0.0f;
      platform_->SetPwmNeutral();
    }

    // ─────────────────────────────────────────────────────────────────────
    // Обновление PWM с slew rate
    // ─────────────────────────────────────────────────────────────────────

    UpdatePwmWithSlewRate(now, commanded_throttle, commanded_steering,
                          applied_throttle, applied_steering, last_pwm_update);

    // ─────────────────────────────────────────────────────────────────────
    // Телеметрия
    // ─────────────────────────────────────────────────────────────────────

    if (telem_handler_) {
      telem_handler_->SetActuatorValues(applied_throttle, applied_steering);
      telem_handler_->Update(now, dt_ms);
    }

    // ─────────────────────────────────────────────────────────────────────
    // Диагностика
    // ─────────────────────────────────────────────────────────────────────

    PrintDiagnostics(now, diag_loop_count, diag_start_ms);
  }
}

PlatformError VehicleControlUnified::Init() {
  if (inited_) {
    return PlatformError::Ok;
  }

  if (!platform_) {
    return PlatformError::TaskCreateFailed;  // Платформа не установлена
  }

  // ───────────────────────────────────────────────────────────────────────
  // Инициализация платформы
  // ───────────────────────────────────────────────────────────────────────

  auto err = platform_->InitPwm();
  if (err != PlatformError::Ok) {
    platform_->Log(LogLevel::Error, "Failed to initialize PWM");
    return err;
  }

  err = platform_->InitFailsafe();
  if (err != PlatformError::Ok) {
    platform_->Log(LogLevel::Error, "Failed to initialize failsafe");
    return err;
  }

  // RC input (опционально)
  err = platform_->InitRc();
  if (err == PlatformError::Ok) {
    rc_enabled_ = true;
  } else {
    rc_enabled_ = false;
    platform_->Log(LogLevel::Warning,
                   "RC input init failed — continuing without RC-in");
  }

  // IMU (опционально)
  err = platform_->InitImu();
  if (err == PlatformError::Ok) {
    imu_enabled_ = true;

    // Загрузка калибровки из NVS
    auto calib_data = platform_->LoadCalib();
    if (calib_data) {
      imu_calib_.SetData(*calib_data);
      if (imu_calib_.IsValid()) {
        const auto& d = imu_calib_.GetData();
        madgwick_.SetVehicleFrame(d.gravity_vec, d.accel_forward_vec, true);
      }
      platform_->Log(LogLevel::Info, "IMU calibration loaded from NVS");
    } else {
      platform_->Log(LogLevel::Info,
                     "No saved IMU calibration — will auto-calibrate at start");
    }

    // Загрузка конфигурации стабилизации из NVS
    auto stab_cfg = platform_->LoadStabilizationConfig();
    if (stab_cfg) {
      stab_config_ = *stab_cfg;
      platform_->Log(LogLevel::Info, "Stabilization config loaded from NVS");
    } else {
      // Использовать значения по умолчанию
      stab_config_.Reset();
      platform_->Log(LogLevel::Info, "Using default stabilization config");
    }

    // Применить конфигурацию к фильтрам
    madgwick_.SetBeta(stab_config_.madgwick_beta);

    // Автокалибровка при старте
    imu_calib_.StartCalibration(CalibMode::Full, 1000);
    platform_->Log(LogLevel::Info,
                   "IMU auto-calibration started (Full, 1000 samples)");
  } else {
    imu_enabled_ = false;
    const int who = platform_->GetImuLastWhoAmI();
    platform_->Log(LogLevel::Warning,
                   "IMU init failed — continuing without IMU");
    if (who >= 0) {
      // Логирование WHO_AM_I для диагностики
    }
  }

  // ───────────────────────────────────────────────────────────────────────
  // Создание компонентов control loop
  // ───────────────────────────────────────────────────────────────────────

  if (!InitializeComponents()) {
    return PlatformError::TaskCreateFailed;
  }

  // ───────────────────────────────────────────────────────────────────────
  // Запуск control loop
  // ───────────────────────────────────────────────────────────────────────

  if (!platform_->CreateTask(ControlTaskEntry, this)) {
    platform_->Log(LogLevel::Error, "Failed to create vehicle control task");
    return PlatformError::TaskCreateFailed;
  }

  inited_ = true;
  platform_->Log(LogLevel::Info,
                 "Vehicle control started (unified architecture)");
  return PlatformError::Ok;
}

bool VehicleControlUnified::InitializeComponents() {
  if (rc_enabled_) {
    rc_handler_.reset(new RcInputHandler(*platform_, RC_IN_POLL_INTERVAL_MS));
  }

  wifi_handler_.reset(new WifiCommandHandler(*platform_, WIFI_CMD_TIMEOUT_MS));

  if (imu_enabled_) {
    imu_handler_.reset(new ImuHandler(*platform_, imu_calib_, madgwick_,
                                      IMU_READ_INTERVAL_MS));
    imu_handler_->SetEnabled(true);
    // Применить LPF cutoff из конфигурации
    imu_handler_->SetLpfCutoff(stab_config_.lpf_cutoff_hz);
  }

  // Создаём пустые handlers если они не были созданы (для телеметрии)
  if (!rc_handler_) {
    rc_handler_.reset(new RcInputHandler(*platform_, 0));
  }
  if (!imu_handler_) {
    imu_handler_.reset(new ImuHandler(*platform_, imu_calib_, madgwick_, 0));
  }

  // Телеметрия (требует const ссылки)
  telem_handler_.reset(new TelemetryHandler(
      *platform_, static_cast<const RcInputHandler&>(*rc_handler_),
      static_cast<const WifiCommandHandler&>(*wifi_handler_),
      static_cast<const ImuHandler&>(*imu_handler_), imu_calib_, madgwick_,
      TELEM_SEND_INTERVAL_MS));

  return true;
}

void VehicleControlUnified::ProcessCalibrationRequest(uint32_t now_ms) {
  int req = calib_request_.exchange(0);  // Атомарное чтение и сброс
  if (req != 0) {
    CalibMode mode = (req == 2) ? CalibMode::Full : CalibMode::GyroOnly;
    int samples = (req == 2) ? 2000 : 1000;
    imu_calib_.StartCalibration(mode, samples);
    platform_->Log(LogLevel::Info, "Calibration stage 1 started");
  }
}

void VehicleControlUnified::ProcessCalibrationCompletion() {
  if (imu_calib_.GetStatus() == CalibStatus::Done) {
    const auto& d = imu_calib_.GetData();
    if (platform_->SaveCalib(imu_calib_.GetData())) {
      platform_->Log(LogLevel::Info, "Calibration saved to NVS");
    }
    // Сбросить статус чтобы не логировать каждую итерацию
    // (в текущей реализации ImuCalibration нет метода сброса статуса)
  } else if (imu_calib_.GetStatus() == CalibStatus::Failed) {
    platform_->Log(LogLevel::Warning, "IMU calibration FAILED");
  }
}

bool VehicleControlUnified::SelectControlSource(float& commanded_throttle,
                                                float& commanded_steering) {
  bool rc_active = rc_handler_ && rc_handler_->IsActive();
  bool wifi_active = wifi_handler_ && wifi_handler_->IsActive();

  if (rc_active) {
    auto cmd = rc_handler_->GetCommand();
    if (cmd) {
      commanded_throttle = cmd->throttle;
      commanded_steering = cmd->steering;
      return true;
    }
  } else if (wifi_active) {
    auto cmd = wifi_handler_->GetCommand();
    if (cmd) {
      commanded_throttle = cmd->throttle;
      commanded_steering = cmd->steering;
      return true;
    }
  }

  return false;
}

void VehicleControlUnified::UpdatePwmWithSlewRate(uint32_t now_ms,
                                                  float commanded_throttle,
                                                  float commanded_steering,
                                                  float& applied_throttle,
                                                  float& applied_steering,
                                                  uint32_t& last_pwm_update) {
  if (now_ms - last_pwm_update >= PWM_UPDATE_INTERVAL_MS) {
    const uint32_t pwm_dt_ms = now_ms - last_pwm_update;
    last_pwm_update = now_ms;

    applied_throttle = ApplySlewRate(commanded_throttle, applied_throttle,
                                     SLEW_RATE_THROTTLE_MAX_PER_SEC, pwm_dt_ms);
    applied_steering = ApplySlewRate(commanded_steering, applied_steering,
                                     SLEW_RATE_STEERING_MAX_PER_SEC, pwm_dt_ms);

    platform_->SetPwm(applied_throttle, applied_steering);
  }
}

void VehicleControlUnified::PrintDiagnostics(uint32_t now_ms,
                                             uint32_t& diag_loop_count,
                                             uint32_t& diag_start_ms) {
  const uint32_t elapsed = now_ms - diag_start_ms;
  if (elapsed >= DIAG_INTERVAL_MS) {
    const uint32_t loop_hz = diag_loop_count * 1000 / elapsed;
    // Логирование диагностики (упрощённое, без ESP_LOGI)
    platform_->Log(LogLevel::Info, "DIAG: control loop running");

    // Статус калибровки
    if (imu_calib_.IsValid()) {
      platform_->Log(LogLevel::Info, "CALIB: valid");
    }

    // Ориентация
    if (imu_handler_ && imu_handler_->IsEnabled()) {
      float pitch_deg = 0.f, roll_deg = 0.f, yaw_deg = 0.f;
      madgwick_.GetEulerDeg(pitch_deg, roll_deg, yaw_deg);
      // Логирование ориентации
    }

    diag_loop_count = 0;
    diag_start_ms = now_ms;
  }
}

void VehicleControlUnified::OnWifiCommand(float throttle, float steering) {
  if (platform_) {
    platform_->SendWifiCommand(throttle, steering);
  }
}

void VehicleControlUnified::StartCalibration(bool full) {
  calib_request_.store(full ? 2 : 1);
}

bool VehicleControlUnified::StartForwardCalibration() {
  return imu_calib_.StartForwardCalibration(2000);
}

const char* VehicleControlUnified::GetCalibStatus() const {
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

int VehicleControlUnified::GetCalibStage() const {
  return imu_calib_.GetCalibStage();
}

void VehicleControlUnified::SetForwardDirection(float fx, float fy, float fz) {
  imu_calib_.SetForwardDirection(fx, fy, fz);
  if (platform_ && platform_->SaveCalib(imu_calib_.GetData())) {
    platform_->Log(LogLevel::Info, "Forward direction set and saved to NVS");
  }
}

bool VehicleControlUnified::SetStabilizationConfig(
    const StabilizationConfig& config, bool save_to_nvs) {
  // Валидация и ограничение параметров
  StabilizationConfig validated_config = config;
  validated_config.Clamp();

  if (!validated_config.IsValid()) {
    if (platform_) {
      platform_->Log(LogLevel::Error, "Invalid stabilization config");
    }
    return false;
  }

  // Применить к фильтрам
  madgwick_.SetBeta(validated_config.madgwick_beta);

  // Применить к LPF (если IMU включен)
  if (imu_handler_) {
    imu_handler_->SetLpfCutoff(validated_config.lpf_cutoff_hz);
  }

  // Сохранить конфигурацию
  stab_config_ = validated_config;

  if (save_to_nvs && platform_) {
    if (platform_->SaveStabilizationConfig(stab_config_)) {
      platform_->Log(LogLevel::Info, "Stabilization config saved to NVS");
    } else {
      platform_->Log(LogLevel::Warning,
                     "Failed to save stabilization config to NVS");
      return false;
    }
  }

  return true;
}

}  // namespace rc_vehicle