#include "vehicle_control_unified.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>

#include "config.hpp"
#include "drive_mode_registry.hpp"
#include "log_format.hpp"
#include "rc_vehicle_common.hpp"
#include "slew_rate.hpp"

namespace rc_vehicle {

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

  // Зарегистрировать control task в Task WDT
  platform_->RegisterTaskWdt();

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

  // Сигнализировать готовность control task (init-ready barrier)
  control_task_ready_.store(true, std::memory_order_release);

  while (true) {
    platform_->DelayUntilNextTick(config::ControlLoopConfig::kPeriodMs);
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
    // EKF: оценка динамического состояния (vx, vy, r → slip angle)
    // ─────────────────────────────────────────────────────────────────────

    if (imu_handler_ && imu_handler_->IsEnabled() && dt_ms > 0) {
      const float dt_sec = static_cast<float>(dt_ms) * 0.001f;
      const auto& imu_data = imu_handler_->GetData();
      // ax, ay в IMU после калибровки в g → конвертируем в м/с²
      constexpr float kG = 9.80665f;
      ekf_.Predict(imu_data.ax * kG, imu_data.ay * kG, dt_sec);
      // gz отфильтрован LPF (dps) → рад/с для EKF
      constexpr float kDegToRad = 3.14159265358979f / 180.0f;
      ekf_.UpdateGyroZ(imu_handler_->GetFilteredGyroZ() * kDegToRad);

      // ZUPT: если |a| ≈ 1g и |gyro_z| мал → машина стоит → vx,vy → 0
      const float accel_mag = std::sqrt(imu_data.ax * imu_data.ax +
                                        imu_data.ay * imu_data.ay +
                                        imu_data.az * imu_data.az);
      constexpr float kZuptAccelThresh = 0.05f;   // |a| - 1g| < 0.05g
      constexpr float kZuptGyroThresh = 3.0f;     // |gyro_z| < 3 dps
      if (std::abs(accel_mag - 1.0f) < kZuptAccelThresh &&
          std::abs(imu_handler_->GetFilteredGyroZ()) < kZuptGyroThresh) {
        ekf_.UpdateZeroVelocity(0.1f);
      }
    }

    // ─────────────────────────────────────────────────────────────────────
    // Обработка запроса калибровки
    // ─────────────────────────────────────────────────────────────────────

    calib_mgr_->ProcessRequest(now);
    calib_mgr_->ProcessCompletion();

    // ─────────────────────────────────────────────────────────────────────
    // Выбор источника управления (RC приоритетнее Wi-Fi)
    // ─────────────────────────────────────────────────────────────────────

    SelectControlSource(commanded_throttle, commanded_steering);

    // ─────────────────────────────────────────────────────────────────────
    // Авто-движение для Forward-калибровки направления
    // RC-пульт имеет приоритет (безопасность): если RC активен, пользователь
    // управляет машиной вручную, авто-движение не применяется.
    // ─────────────────────────────────────────────────────────────────────

    const bool rc_active_now = rc_handler_ && rc_handler_->IsActive();
    if (calib_mgr_->IsAutoForwardActive() && !rc_active_now) {
      commanded_throttle = calib_mgr_->GetAutoForwardThrottle();
      commanded_steering = 0.0f;
    }

    // ─────────────────────────────────────────────────────────────────────
    // Плавное нарастание/спад веса стабилизации и переход между режимами
    // ─────────────────────────────────────────────────────────────────────

    stab_mgr_->UpdateWeights(dt_ms);

    const DriveMode drive_mode = stab_mgr_ ? stab_mgr_->GetConfig().mode : DriveMode::Normal;
    const auto& strategy = DriveModeRegistry::Get(drive_mode);
    const auto traits = strategy.GetTraits();

    // ─────────────────────────────────────────────────────────────────────
    // Kids Mode: применить ограничения газа/руля и anti-spin
    // (активируется через traits.apply_input_limits)
    // ─────────────────────────────────────────────────────────────────────

    if (traits.apply_input_limits) {
      kids_processor_.Process(commanded_throttle, commanded_steering, dt_ms);
    }

    // ─────────────────────────────────────────────────────────────────────
    // Стабилизационный pipeline (управляется ModeTraits)
    // ─────────────────────────────────────────────────────────────────────

    const float stab_weight = stab_mgr_->GetStabilizationWeight();
    const float mode_weight = stab_mgr_->GetModeTransitionWeight();

    if (traits.yaw_rate_active)
      yaw_ctrl_.Process(commanded_steering, stab_weight, mode_weight, dt_ms);

    if (traits.pitch_comp_active)
      pitch_ctrl_.Process(commanded_throttle, stab_weight);

    if (traits.slip_angle_active)
      slip_ctrl_.Process(commanded_throttle, stab_weight, mode_weight, dt_ms);

    if (traits.oversteer_guard_active)
      oversteer_guard_.Process(commanded_throttle, dt_ms,
                               traits.oversteer_reduces_throttle);

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
      yaw_ctrl_.Reset();
      slip_ctrl_.Reset();
      oversteer_guard_.Reset();
      kids_processor_.Reset();
      ekf_.Reset();
      stab_mgr_->ResetWeights();       // Сброс весов стабилизации
      telem_mgr_->ResetLastLogTime();  // Сброс таймера лога
      calib_mgr_->StopAutoForward();   // Прервать авто-движение при failsafe
      platform_->SetPwmNeutral();
    }

    // ─────────────────────────────────────────────────────────────────────
    // Обновление PWM с slew rate
    // ─────────────────────────────────────────────────────────────────────

    if (traits.use_slew_rate) {
      UpdatePwmWithSlewRate(now, commanded_throttle, commanded_steering,
                            applied_throttle, applied_steering, last_pwm_update);
    } else {
      applied_throttle = commanded_throttle;
      applied_steering = commanded_steering;
      platform_->SetPwm(applied_throttle, applied_steering);
    }

    // ─────────────────────────────────────────────────────────────────────
    // Телеметрия
    // ─────────────────────────────────────────────────────────────────────

    if (telem_handler_) {
      TelemetrySnapshot snap;
      snap.rc_ok = rc_handler_ && rc_handler_->IsActive();
      snap.wifi_ok = wifi_handler_ && wifi_handler_->IsActive();
      snap.throttle = applied_throttle;
      snap.steering = applied_steering;

      // Kids Mode status (routing определяется drive_mode/traits)
      snap.kids_mode_active = (drive_mode == DriveMode::Kids);
      snap.kids_anti_spin_active = kids_processor_.IsAntiSpinActive();
      snap.kids_throttle_limit =
          stab_mgr_->GetConfig().kids_mode.throttle_limit;

      if (imu_handler_ && imu_handler_->IsEnabled()) {
        snap.imu_enabled = true;
        snap.imu_data = imu_handler_->GetData();
        snap.filtered_gz = imu_handler_->GetFilteredGyroZ();
        snap.forward_accel = imu_calib_.GetForwardAccel(snap.imu_data);
        madgwick_.GetEulerDeg(snap.pitch_deg, snap.roll_deg, snap.yaw_deg);
        snap.calib_status = imu_calib_.GetStatus();
        snap.calib_stage = imu_calib_.GetCalibStage();
        snap.calib_valid = imu_calib_.IsValid();
        if (snap.calib_valid) {
          snap.calib_data = imu_calib_.GetData();
        }
        snap.ekf_available = true;
        snap.ekf_vx = ekf_.GetVx();
        snap.ekf_vy = ekf_.GetVy();
        snap.ekf_yaw_rate = ekf_.GetYawRate();
        snap.ekf_slip_deg = ekf_.GetSlipAngleDeg();
        snap.ekf_speed_ms = ekf_.GetSpeedMs();
        snap.oversteer_available = true;
        snap.oversteer_active = oversteer_guard_.IsActive();
      }
      telem_handler_->Update(now, snap);
    }

    // ─────────────────────────────────────────────────────────────────────
    // Запись в кольцевой буфер телеметрии (Phase 4.3) — 20 Hz
    // ─────────────────────────────────────────────────────────────────────

    if (imu_handler_ && imu_handler_->IsEnabled()) {
      const uint32_t last_log = telem_mgr_->GetLastLogTime();
      if (now - last_log >= config::TelemetryConfig::kSendIntervalMs) {
        TelemetryLogFrame frame;
        frame.ts_ms = now;
        const auto& d = imu_handler_->GetData();
        frame.ax = d.ax;
        frame.ay = d.ay;
        frame.az = d.az;
        frame.gx = d.gx;
        frame.gy = d.gy;
        frame.gz = d.gz;
        frame.vx = ekf_.GetVx();
        frame.vy = ekf_.GetVy();
        frame.slip_deg = ekf_.GetSlipAngleDeg();
        frame.speed_ms = ekf_.GetSpeedMs();
        frame.throttle = applied_throttle;
        frame.steering = applied_steering;
        madgwick_.GetEulerDeg(frame.pitch_deg, frame.roll_deg, frame.yaw_deg);
        frame.yaw_rate_dps = imu_handler_->GetFilteredGyroZ();
        frame.oversteer_active = oversteer_guard_.IsActive() ? 1.0f : 0.0f;
        telem_mgr_->Push(frame);
        telem_mgr_->SetLastLogTime(now);
      }
    }

    // ─────────────────────────────────────────────────────────────────────
    // Диагностика
    // ─────────────────────────────────────────────────────────────────────

    PrintDiagnostics(now, diag_loop_count, diag_start_ms);

    // Кормить watchdog — если цикл зависнет, WDT перезагрузит устройство
    platform_->FeedTaskWdt();
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

  auto pwm_result = platform_->InitPwm();
  if (IsError(pwm_result)) {
    platform_->Log(LogLevel::Error, "Failed to initialize PWM");
    return GetError(pwm_result);
  }

  auto failsafe_result = platform_->InitFailsafe();
  if (IsError(failsafe_result)) {
    platform_->Log(LogLevel::Error, "Failed to initialize failsafe");
    return GetError(failsafe_result);
  }

  // RC input (опционально)
  auto rc_result = platform_->InitRc();
  if (IsOk(rc_result)) {
    rc_enabled_ = true;
  } else {
    rc_enabled_ = false;
    platform_->Log(LogLevel::Warning,
                   "RC input init failed — continuing without RC-in");
  }

  // IMU (опционально)
  auto imu_result = platform_->InitImu();
  if (IsOk(imu_result)) {
    imu_enabled_ = true;

    // Создание менеджеров (должно быть до загрузки конфигурации)
    calib_mgr_.reset(
        new CalibrationManager(*platform_, imu_calib_, madgwick_, &ekf_));
    stab_mgr_.reset(new StabilizationManager(*platform_, madgwick_, yaw_ctrl_,
                                             slip_ctrl_, nullptr));
    telem_mgr_.reset(new TelemetryManager());

    // Загрузка калибровки из NVS
    calib_mgr_->LoadFromNvs();

    // Загрузка конфигурации стабилизации из NVS
    stab_mgr_->LoadFromNvs();

    // Применить конфигурацию к фильтрам
    stab_mgr_->ApplyConfig();

    // Автокалибровка при старте
    calib_mgr_->StartAutoCalibration();
  } else {
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
  }

  // ───────────────────────────────────────────────────────────────────────
  // Инициализация кольцевого буфера телеметрии (Phase 4.3)
  // 5000 кадров × ~60 байт ≈ 293 КБ → выделяется из PSRAM
  // ───────────────────────────────────────────────────────────────────────

  if (!telem_mgr_->Init(config::TelemetryLogConfig::kCapacityFrames)) {
    platform_->Log(
        LogLevel::Warning,
        "TelemetryLog: failed to allocate (no PSRAM?), log disabled");
  } else {
    LogFormat fmt;
    fmt << "TelemetryLog: allocated "
        << static_cast<unsigned>(config::TelemetryLogConfig::kCapacityFrames)
        << " frames";
    platform_->Log(LogLevel::Info, fmt.str());
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

  auto task_result = platform_->CreateTask(ControlTaskEntry, this);
  if (IsError(task_result)) {
    platform_->Log(LogLevel::Error, "Failed to create vehicle control task");
    return GetError(task_result);
  }

  inited_ = true;
  platform_->Log(LogLevel::Info,
                 "Vehicle control started (unified architecture)");
  return PlatformError::Ok;
}

bool VehicleControlUnified::InitializeComponents() {
  if (rc_enabled_) {
    rc_handler_.reset(
        new RcInputHandler(*platform_, config::RcInputConfig::kPollIntervalMs));
  }

  wifi_handler_.reset(new WifiCommandHandler(
      *platform_, config::WifiConfig::kCommandTimeoutMs));

  if (imu_enabled_) {
    imu_handler_.reset(new ImuHandler(*platform_, imu_calib_, madgwick_,
                                      config::ImuConfig::kReadIntervalMs));
    imu_handler_->SetEnabled(true);
    // Применить LPF cutoff из конфигурации
    imu_handler_->SetLpfCutoff(stab_mgr_->GetConfig().filter.lpf_cutoff_hz);
    // Обновить указатель на imu_handler в stab_mgr
    stab_mgr_.reset(new StabilizationManager(*platform_, madgwick_, yaw_ctrl_,
                                             slip_ctrl_, imu_handler_.get()));
    stab_mgr_->LoadFromNvs();
    stab_mgr_->ApplyConfig();
  }

  // Создаём пустые handlers если они не были созданы (для телеметрии)
  if (!rc_handler_) {
    rc_handler_.reset(new RcInputHandler(*platform_, 0));
  }
  if (!imu_handler_) {
    imu_handler_.reset(new ImuHandler(*platform_, imu_calib_, madgwick_, 0));
  }

  // Инициализация стратегий стабилизации
  const auto& cfg = stab_mgr_->GetConfig();
  yaw_ctrl_.Init(cfg, ekf_, imu_handler_.get());
  pitch_ctrl_.Init(cfg, madgwick_, imu_handler_.get());
  slip_ctrl_.Init(cfg, ekf_, imu_handler_.get());
  oversteer_guard_.Init(cfg, ekf_, imu_handler_.get());
  kids_processor_.Init(cfg, ekf_, imu_handler_.get());

  // Телеметрия
  telem_handler_.reset(new TelemetryHandler(
      *platform_, config::TelemetryConfig::kSendIntervalMs));

  return true;
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
  if (now_ms - last_pwm_update >= config::PwmConfig::kUpdateIntervalMs) {
    const uint32_t pwm_dt_ms = now_ms - last_pwm_update;
    last_pwm_update = now_ms;

    applied_throttle =
        ApplySlewRate(commanded_throttle, applied_throttle,
                      config::SlewRateConfig::kThrottleMaxPerSec, pwm_dt_ms);
    applied_steering =
        ApplySlewRate(commanded_steering, applied_steering,
                      config::SlewRateConfig::kSteeringMaxPerSec, pwm_dt_ms);

    platform_->SetPwm(applied_throttle, applied_steering);
  }
}

void VehicleControlUnified::PrintDiagnostics(uint32_t now_ms,
                                             uint32_t& diag_loop_count,
                                             uint32_t& diag_start_ms) {
  const uint32_t elapsed = now_ms - diag_start_ms;
  if (elapsed >= config::DiagnosticsConfig::kIntervalMs) {
    const uint32_t loop_hz =
        (elapsed > 0) ? (diag_loop_count * 1000u / elapsed) : 0u;
    last_loop_hz_.store(loop_hz, std::memory_order_relaxed);

    const auto& cfg = stab_mgr_->GetConfig();
    const float stab_weight = stab_mgr_->GetStabilizationWeight();

    {
      LogFormat fmt;
      fmt << "DIAG: loop=" << static_cast<unsigned>(loop_hz)
          << " Hz  stab=" << (cfg.enabled ? "ON" : "OFF")
          << " (w=" << std::fixed << std::setprecision(2) << stab_weight << ")";
      platform_->Log(LogLevel::Info, fmt.str());
    }

    if (imu_handler_ && imu_handler_->IsEnabled()) {
      float pitch_deg = 0.f, roll_deg = 0.f, yaw_deg = 0.f;
      madgwick_.GetEulerDeg(pitch_deg, roll_deg, yaw_deg);

      {
        LogFormat fmt;
        fmt << "IMU: P=" << std::fixed << std::setprecision(1) << pitch_deg
            << " R=" << roll_deg << " Y=" << yaw_deg
            << " deg  gz=" << imu_handler_->GetFilteredGyroZ() << " dps";
        platform_->Log(LogLevel::Info, fmt.str());
      }

      {
        LogFormat fmt;
        fmt << "EKF: vx=" << std::fixed << std::setprecision(2) << ekf_.GetVx()
            << " vy=" << ekf_.GetVy() << " m/s  slip=" << std::setprecision(1)
            << ekf_.GetSlipAngleDeg() << " deg";
        platform_->Log(LogLevel::Info, fmt.str());
      }
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

std::vector<SelfTestItem> VehicleControlUnified::RunSelfTest() const {
  SelfTestInput input;

  // Control loop frequency
  input.loop_hz = last_loop_hz_.load(std::memory_order_relaxed);

  // IMU
  if (imu_handler_) {
    input.imu_enabled = imu_handler_->IsEnabled();
    const auto& imu = imu_handler_->GetData();
    input.gyro_x_dps = imu.gx;
    input.gyro_y_dps = imu.gy;
    input.gyro_z_dps = imu.gz;
    input.accel_x_g = imu.ax;
    input.accel_y_g = imu.ay;
    input.accel_z_g = imu.az;
  }

  // Madgwick
  {
    float pitch = 0, roll = 0, yaw = 0;
    madgwick_.GetEulerDeg(pitch, roll, yaw);
    input.pitch_deg = pitch;
    input.roll_deg = roll;
  }

  // EKF
  input.ekf_vx = ekf_.GetVx();
  input.ekf_vy = ekf_.GetVy();

  // Failsafe — check via telemetry snapshot (failsafe is private in platform)
  // We check if RC or WiFi are active; if neither, failsafe would be active
  bool rc_ok = rc_handler_ && rc_handler_->IsActive();
  bool wifi_ok = wifi_handler_ && wifi_handler_->IsActive();
  // Self-test is called from WS handler, so WiFi should be active
  input.failsafe_active = !rc_ok && !wifi_ok;

  // Calibration
  input.calib_valid = imu_calib_.IsValid();

  // TelemetryLog
  if (telem_mgr_) {
    size_t count = 0, cap = 0;
    telem_mgr_->GetLogInfo(count, cap);
    input.log_capacity = cap;
  }

  // PWM — assume ok if platform exists and is initialized
  input.pwm_status = (platform_ && inited_) ? 0 : -1;

  return SelfTest::Run(input);
}

}  // namespace rc_vehicle