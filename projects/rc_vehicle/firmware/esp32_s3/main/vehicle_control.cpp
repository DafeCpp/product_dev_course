#include "vehicle_control.hpp"

#include <memory>

#include "config.hpp"
#include "control_components.hpp"
#include "esp_log.h"
#include "imu_calibration.hpp"
#include "madgwick_filter.hpp"
#include "rc_vehicle_common.hpp"
#include "slew_rate.hpp"
#include "vehicle_control_platform_esp32.hpp"

static const char* TAG = "vehicle_control";

// ═════════════════════════════════════════════════════════════════════════
// VehicleControl Implementation
// ═════════════════════════════════════════════════════════════════════════

VehicleControl& VehicleControl::Instance() {
  static VehicleControl s_instance;
  return s_instance;
}

VehicleControl::VehicleControl()
    : platform_(new rc_vehicle::VehicleControlPlatformEsp32()),
      imu_calib_(),
      madgwick_(),
      rc_handler_(nullptr),
      wifi_handler_(nullptr),
      imu_handler_(nullptr),
      telem_handler_(nullptr) {}

VehicleControl::~VehicleControl() = default;

void VehicleControl::ControlTaskEntry(void* arg) {
  auto* self = static_cast<VehicleControl*>(arg);
  if (self) self->ControlTaskLoop();
}

void VehicleControl::ControlTaskLoop() {
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

    {
      int req = calib_request_;
      if (req != 0) {
        calib_request_ = 0;
        CalibMode mode = (req == 2) ? CalibMode::Full : CalibMode::GyroOnly;
        int samples = (req == 2) ? 2000 : 1000;
        imu_calib_.StartCalibration(mode, samples);
        ESP_LOGI(TAG, "Calibration stage 1 started (mode=%d, samples=%d)", req,
                 samples);
      }
    }

    // Проверка завершения калибровки
    if (imu_calib_.GetStatus() == CalibStatus::Done) {
      const auto& d = imu_calib_.GetData();
      ESP_LOGI(TAG,
               "IMU calibration DONE — gyro bias: [%.3f, %.3f, %.3f] "
               "accel bias: [%.4f, %.4f, %.4f]",
               d.gyro_bias[0], d.gyro_bias[1], d.gyro_bias[2], d.accel_bias[0],
               d.accel_bias[1], d.accel_bias[2]);
      if (platform_->SaveCalib(imu_calib_.GetData())) {
        ESP_LOGI(TAG, "Calibration saved to NVS");
      }
    } else if (imu_calib_.GetStatus() == CalibStatus::Failed) {
      ESP_LOGW(TAG, "IMU calibration FAILED");
    }

    // ─────────────────────────────────────────────────────────────────────
    // Выбор источника управления (RC приоритетнее Wi-Fi)
    // ─────────────────────────────────────────────────────────────────────

    bool rc_active = rc_handler_ && rc_handler_->IsActive();
    bool wifi_active = wifi_handler_ && wifi_handler_->IsActive();

    if (rc_active) {
      auto cmd = rc_handler_->GetCommand();
      if (cmd) {
        commanded_throttle = cmd->throttle;
        commanded_steering = cmd->steering;
      }
    } else if (wifi_active) {
      auto cmd = wifi_handler_->GetCommand();
      if (cmd) {
        commanded_throttle = cmd->throttle;
        commanded_steering = cmd->steering;
      }
    }

    // ─────────────────────────────────────────────────────────────────────
    // Failsafe
    // ─────────────────────────────────────────────────────────────────────

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

    if (now - last_pwm_update >= PWM_UPDATE_INTERVAL_MS) {
      const uint32_t pwm_dt_ms = now - last_pwm_update;
      last_pwm_update = now;

      applied_throttle =
          ApplySlewRate(commanded_throttle, applied_throttle,
                        SLEW_RATE_THROTTLE_MAX_PER_SEC, pwm_dt_ms);
      applied_steering =
          ApplySlewRate(commanded_steering, applied_steering,
                        SLEW_RATE_STEERING_MAX_PER_SEC, pwm_dt_ms);

      platform_->SetPwm(applied_throttle, applied_steering);
    }

    // ─────────────────────────────────────────────────────────────────────
    // Телеметрия
    // ─────────────────────────────────────────────────────────────────────

    if (telem_handler_) {
      telem_handler_->SetActuatorValues(applied_throttle, applied_steering);
      telem_handler_->Update(now, dt_ms);
    }

    // ─────────────────────────────────────────────────────────────────────
    // Диагностика (каждые 5 секунд)
    // ─────────────────────────────────────────────────────────────────────

    const uint32_t elapsed = now - diag_start_ms;
    if (elapsed >= 5000) {
      const uint32_t loop_hz = diag_loop_count * 1000 / elapsed;
      ESP_LOGI(TAG, "DIAG: loop=%lu Hz, iters=%lu in %lu ms",
               (unsigned long)loop_hz, (unsigned long)diag_loop_count,
               (unsigned long)elapsed);

      // Статус калибровки
      const char* calib_str = "off";
      switch (imu_calib_.GetStatus()) {
        case CalibStatus::Idle:
          calib_str = "idle";
          break;
        case CalibStatus::Collecting:
          calib_str = "collecting";
          break;
        case CalibStatus::Done:
          calib_str = "done";
          break;
        case CalibStatus::Failed:
          calib_str = "failed";
          break;
      }
      ESP_LOGI(TAG, "CALIB: status=%s valid=%s", calib_str,
               imu_calib_.IsValid() ? "YES" : "NO");

      // Ориентация
      if (imu_handler_ && imu_handler_->IsEnabled()) {
        float pitch_deg = 0.f, roll_deg = 0.f, yaw_deg = 0.f;
        madgwick_.GetEulerDeg(pitch_deg, roll_deg, yaw_deg);
        const auto& imu_data = imu_handler_->GetData();
        ESP_LOGI(TAG,
                 "IMU: ax=%.3f ay=%.3f az=%.3f | orientation pitch=%.1f "
                 "roll=%.1f yaw=%.1f deg",
                 imu_data.ax, imu_data.ay, imu_data.az, pitch_deg, roll_deg,
                 yaw_deg);
      }

      diag_loop_count = 0;
      diag_start_ms = now;
    }
  }
}

esp_err_t VehicleControl::Init() {
  if (inited_) return ESP_OK;

  // ───────────────────────────────────────────────────────────────────────
  // Инициализация платформы
  // ───────────────────────────────────────────────────────────────────────

  auto err = platform_->InitPwm();
  if (err != rc_vehicle::PlatformError::Ok) {
    ESP_LOGE(TAG, "Failed to initialize PWM");
    return ESP_FAIL;
  }

  err = platform_->InitFailsafe();
  if (err != rc_vehicle::PlatformError::Ok) {
    ESP_LOGE(TAG, "Failed to initialize failsafe");
    return ESP_FAIL;
  }

  // RC input (опционально)
  err = platform_->InitRc();
  if (err == rc_vehicle::PlatformError::Ok) {
    rc_enabled_ = true;
  } else {
    rc_enabled_ = false;
    ESP_LOGW(TAG, "RC input init failed — continuing without RC-in");
  }

  // IMU (опционально)
  err = platform_->InitImu();
  if (err == rc_vehicle::PlatformError::Ok) {
    imu_enabled_ = true;

    // Загрузка калибровки из NVS
    auto calib_data = platform_->LoadCalib();
    if (calib_data) {
      imu_calib_.SetData(*calib_data);
      if (imu_calib_.IsValid()) {
        const auto& d = imu_calib_.GetData();
        madgwick_.SetVehicleFrame(d.gravity_vec, d.accel_forward_vec, true);
      }
      ESP_LOGI(TAG, "IMU calibration loaded from NVS (valid=%d)",
               calib_data->valid);
    } else {
      ESP_LOGI(TAG, "No saved IMU calibration — will auto-calibrate at start");
    }

    // Автокалибровка при старте
    imu_calib_.StartCalibration(CalibMode::Full, 1000);
    ESP_LOGI(TAG, "IMU auto-calibration started (Full, 1000 samples)");
  } else {
    imu_enabled_ = false;
    const int who = platform_->GetImuLastWhoAmI();
    ESP_LOGW(TAG, "IMU init failed — continuing without IMU");
    if (who >= 0) {
      ESP_LOGW(TAG,
               "IMU WHO_AM_I=0x%02X (expected 0x68 MPU-6050 or 0x70 MPU-6500)",
               who);
    }
  }

  // ───────────────────────────────────────────────────────────────────────
  // Создание компонентов control loop
  // ───────────────────────────────────────────────────────────────────────

  if (rc_enabled_) {
    rc_handler_.reset(
        new rc_vehicle::RcInputHandler(*platform_, RC_IN_POLL_INTERVAL_MS));
  }

  wifi_handler_.reset(
      new rc_vehicle::WifiCommandHandler(*platform_, WIFI_CMD_TIMEOUT_MS));

  if (imu_enabled_) {
    imu_handler_.reset(new rc_vehicle::ImuHandler(
        *platform_, imu_calib_, madgwick_, IMU_READ_INTERVAL_MS));
    imu_handler_->SetEnabled(true);
  }

  // Создаём пустые handlers если они не были созданы (для телеметрии)
  if (!rc_handler_) {
    rc_handler_.reset(new rc_vehicle::RcInputHandler(*platform_, 0));
  }
  if (!imu_handler_) {
    imu_handler_.reset(
        new rc_vehicle::ImuHandler(*platform_, imu_calib_, madgwick_, 0));
  }

  // Телеметрия (требует const ссылки)
  telem_handler_.reset(new rc_vehicle::TelemetryHandler(
      *platform_, static_cast<const rc_vehicle::RcInputHandler&>(*rc_handler_),
      static_cast<const rc_vehicle::WifiCommandHandler&>(*wifi_handler_),
      static_cast<const rc_vehicle::ImuHandler&>(*imu_handler_),
      imu_calib_, madgwick_, TELEM_SEND_INTERVAL_MS));

  // ───────────────────────────────────────────────────────────────────────
  // Запуск control loop
  // ───────────────────────────────────────────────────────────────────────

  if (!platform_->CreateTask(ControlTaskEntry, this)) {
    ESP_LOGE(TAG, "Failed to create vehicle control task");
    return ESP_FAIL;
  }

  inited_ = true;
  ESP_LOGI(TAG, "Vehicle control started (new architecture)");
  return ESP_OK;
}

void VehicleControl::OnWifiCommand(float throttle, float steering) {
  platform_->SendWifiCommand(throttle, steering);
}

void VehicleControl::StartCalibration(bool full) {
  calib_request_ = full ? 2 : 1;
}

bool VehicleControl::StartForwardCalibration() {
  return imu_calib_.StartForwardCalibration(2000);
}

const char* VehicleControl::GetCalibStatus() const {
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

int VehicleControl::GetCalibStage() const { return imu_calib_.GetCalibStage(); }

void VehicleControl::SetForwardDirection(float fx, float fy, float fz) {
  imu_calib_.SetForwardDirection(fx, fy, fz);
  if (platform_->SaveCalib(imu_calib_.GetData())) {
    const auto& v = imu_calib_.GetData().accel_forward_vec;
    ESP_LOGI(TAG, "Forward direction set: vec=[%.3f, %.3f, %.3f], saved to NVS",
             v[0], v[1], v[2]);
  }
}
