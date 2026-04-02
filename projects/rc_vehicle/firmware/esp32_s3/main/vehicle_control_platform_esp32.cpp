#include "vehicle_control_platform_esp32.hpp"

#include <cstring>

#include "config.hpp"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "failsafe.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "imu.hpp"
#include "imu_calibration_nvs.hpp"
#include "mag_calibration_nvs.hpp"
#include "mag.hpp"
#include "pwm_control.hpp"
#include "rc_input.hpp"
#include "rc_vehicle_common.hpp"
#include "stabilization_config_nvs.hpp"
#include "websocket_server.hpp"

namespace rc_vehicle {

using rc_vehicle::Err;
using rc_vehicle::Ok;
using rc_vehicle::Unit;

static const char* TAG = "platform_esp32";

// Константы для задачи control loop
static constexpr uint32_t CONTROL_TASK_STACK = 12288;
static constexpr UBaseType_t CONTROL_TASK_PRIORITY = configMAX_PRIORITIES - 1;

// ─────────────────────────────────────────────────────────────────────────
// Конструктор / Деструктор
// ─────────────────────────────────────────────────────────────────────────

VehicleControlPlatformEsp32::VehicleControlPlatformEsp32()
    : failsafe_(FAILSAFE_TIMEOUT_MS) {
  cmd_queue_ = xQueueCreate(1, sizeof(WifiCmd));
}

VehicleControlPlatformEsp32::~VehicleControlPlatformEsp32() {
  if (cmd_queue_) {
    vQueueDelete(cmd_queue_);
  }
}

// ─────────────────────────────────────────────────────────────────────────
// Инициализация
// ─────────────────────────────────────────────────────────────────────────

Result<Unit, PlatformError> VehicleControlPlatformEsp32::InitPwm() {
  return (PwmControlInit() == 0)
             ? Ok<Unit, PlatformError>(Unit{})
             : Err<Unit, PlatformError>(PlatformError::PwmInitFailed);
}

Result<Unit, PlatformError> VehicleControlPlatformEsp32::InitRc() {
  return (RcInputInit() == 0)
             ? Ok<Unit, PlatformError>(Unit{})
             : Err<Unit, PlatformError>(PlatformError::RcInitFailed);
}

Result<Unit, PlatformError> VehicleControlPlatformEsp32::InitImu() {
  return (ImuInit() == 0)
             ? Ok<Unit, PlatformError>(Unit{})
             : Err<Unit, PlatformError>(PlatformError::ImuInitFailed);
}

Result<Unit, PlatformError> VehicleControlPlatformEsp32::InitFailsafe() {
  // Failsafe инициализируется в конструкторе
  return Ok<Unit, PlatformError>(Unit{});
}

// ─────────────────────────────────────────────────────────────────────────
// Время
// ─────────────────────────────────────────────────────────────────────────

uint32_t VehicleControlPlatformEsp32::GetTimeMs() const noexcept {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

uint64_t VehicleControlPlatformEsp32::GetTimeUs() const noexcept {
  return esp_timer_get_time();
}

// ─────────────────────────────────────────────────────────────────────────
// Логирование
// ─────────────────────────────────────────────────────────────────────────

void VehicleControlPlatformEsp32::Log(LogLevel level,
                                      std::string_view msg) const {
  // Создаём null-terminated строку для ESP_LOG*
  char buffer[256];
  size_t len = std::min(msg.size(), sizeof(buffer) - 1);
  std::memcpy(buffer, msg.data(), len);
  buffer[len] = '\0';

  switch (level) {
    case LogLevel::Info:
      ESP_LOGI(TAG, "%s", buffer);
      break;
    case LogLevel::Warning:
      ESP_LOGW(TAG, "%s", buffer);
      break;
    case LogLevel::Error:
      ESP_LOGE(TAG, "%s", buffer);
      break;
  }
}

// ─────────────────────────────────────────────────────────────────────────
// IMU
// ─────────────────────────────────────────────────────────────────────────

std::optional<ImuData> VehicleControlPlatformEsp32::ReadImu() {
  ImuData data{};
  if (ImuRead(data) == 0) {
    return data;
  }
  return std::nullopt;
}

int VehicleControlPlatformEsp32::GetImuLastWhoAmI() const noexcept {
  return ImuGetLastWhoAmI();
}

// ─────────────────────────────────────────────────────────────────────────
// Магнитометр
// ─────────────────────────────────────────────────────────────────────────

bool VehicleControlPlatformEsp32::InitMag() {
  return MagInit() == 0;
}

std::optional<MagData> VehicleControlPlatformEsp32::ReadMag() {
  MagData data{};
  if (MagRead(data) == 0) {
    return data;
  }
  return std::nullopt;
}

const char* VehicleControlPlatformEsp32::GetMagSensorName() const noexcept {
  return MagGetSensorName();
}

bool VehicleControlPlatformEsp32::SaveMagCalib(const MagCalibData& data) {
  return mag_nvs::Save(data) == ESP_OK;
}

bool VehicleControlPlatformEsp32::LoadMagCalib(MagCalibData& data) {
  return mag_nvs::Load(data) == ESP_OK;
}

bool VehicleControlPlatformEsp32::EraseMagCalib() {
  return mag_nvs::Erase() == ESP_OK;
}

// ─────────────────────────────────────────────────────────────────────────
// Калибровка IMU
// ─────────────────────────────────────────────────────────────────────────

std::optional<ImuCalibData> VehicleControlPlatformEsp32::LoadCalib() {
  ImuCalibData data{};
  if (imu_nvs::Load(data) == ESP_OK && data.valid) {
    return data;
  }
  return std::nullopt;
}

Result<Unit, PlatformError> VehicleControlPlatformEsp32::SaveCalib(
    const ImuCalibData& data) {
  return (imu_nvs::Save(data) == ESP_OK)
             ? Ok<Unit, PlatformError>(Unit{})
             : Err<Unit, PlatformError>(PlatformError::CalibSaveFailed);
}

Result<Unit, PlatformError> VehicleControlPlatformEsp32::SaveComOffset(
    const float offset[2]) {
  return (imu_nvs::SaveComOffset(offset) == ESP_OK)
             ? Ok<Unit, PlatformError>(Unit{})
             : Err<Unit, PlatformError>(PlatformError::CalibSaveFailed);
}

bool VehicleControlPlatformEsp32::LoadComOffset(float offset[2]) {
  return imu_nvs::LoadComOffset(offset) == ESP_OK;
}

// ─────────────────────────────────────────────────────────────────────────
// Stabilization Config
// ─────────────────────────────────────────────────────────────────────────

std::optional<StabilizationConfig>
VehicleControlPlatformEsp32::LoadStabilizationConfig() {
  StabilizationConfig config{};
  if (stab_config_nvs::Load(config) == ESP_OK && config.IsValid()) {
    return config;
  }
  return std::nullopt;
}

Result<Unit, PlatformError>
VehicleControlPlatformEsp32::SaveStabilizationConfig(
    const StabilizationConfig& config) {
  return (stab_config_nvs::Save(config) == ESP_OK)
             ? Ok<Unit, PlatformError>(Unit{})
             : Err<Unit, PlatformError>(PlatformError::CalibSaveFailed);
}

// ─────────────────────────────────────────────────────────────────────────
// RC Input
// ─────────────────────────────────────────────────────────────────────────

std::optional<RcCommand> VehicleControlPlatformEsp32::GetRc() {
  auto throttle = RcInputReadThrottle();
  auto steering = RcInputReadSteering();

  if (throttle.has_value() && steering.has_value()) {
    return RcCommand{.throttle = *throttle, .steering = *steering};
  }
  return std::nullopt;
}

// ─────────────────────────────────────────────────────────────────────────
// PWM Output
// ─────────────────────────────────────────────────────────────────────────

void VehicleControlPlatformEsp32::SetPwm(float throttle,
                                         float steering) noexcept {
  PwmControlSetThrottle(throttle);
  PwmControlSetSteering(steering);
}

void VehicleControlPlatformEsp32::SetPwmNeutral() noexcept {
  PwmControlSetNeutral();
}

// ─────────────────────────────────────────────────────────────────────────
// Failsafe
// ─────────────────────────────────────────────────────────────────────────

bool VehicleControlPlatformEsp32::FailsafeUpdate(bool rc_active,
                                                 bool wifi_active) {
  uint32_t now_ms = GetTimeMs();
  auto state = failsafe_.Update(now_ms, rc_active, wifi_active);
  return state == FailsafeState::Active;
}

bool VehicleControlPlatformEsp32::FailsafeIsActive() const noexcept {
  return failsafe_.IsActive();
}

// ─────────────────────────────────────────────────────────────────────────
// WebSocket
// ─────────────────────────────────────────────────────────────────────────

unsigned VehicleControlPlatformEsp32::GetWebSocketClientCount() const noexcept {
  return WebSocketGetClientCount();
}

void VehicleControlPlatformEsp32::SendTelem(std::string_view json) {
  // Не блокировать цикл управления на TCP: ставим в очередь, отправляет ws_telem
  char buffer[2048];
  if (json.size() >= sizeof(buffer)) {
    ESP_LOGW(TAG, "Telem JSON truncated: %zu > %zu bytes — увеличьте буфер",
             json.size(), sizeof(buffer) - 1);
  }
  size_t len = std::min(json.size(), sizeof(buffer) - 1);
  std::memcpy(buffer, json.data(), len);
  buffer[len] = '\0';
  WebSocketEnqueueTelem(buffer);
}

// ─────────────────────────────────────────────────────────────────────────
// Wi-Fi команды
// ─────────────────────────────────────────────────────────────────────────

std::optional<RcCommand> VehicleControlPlatformEsp32::TryReceiveWifiCommand() {
  if (!cmd_queue_) return std::nullopt;

  WifiCmd cmd;
  if (xQueueReceive(cmd_queue_, &cmd, 0) == pdTRUE) {
    return RcCommand{.throttle = cmd.throttle, .steering = cmd.steering};
  }
  return std::nullopt;
}

void VehicleControlPlatformEsp32::SendWifiCommand(float throttle,
                                                  float steering) {
  if (!cmd_queue_) return;

  WifiCmd cmd = {
      .throttle = ClampNormalized(throttle),
      .steering = ClampNormalized(steering),
  };
  xQueueOverwrite(cmd_queue_, &cmd);
}

// ─────────────────────────────────────────────────────────────────────────
// Задачи и синхронизация
// ─────────────────────────────────────────────────────────────────────────

Result<Unit, PlatformError> VehicleControlPlatformEsp32::CreateTask(
    void (*entry)(void*), void* arg) {
  BaseType_t result =
      xTaskCreatePinnedToCore(entry, "vehicle_ctrl", CONTROL_TASK_STACK, arg,
                              CONTROL_TASK_PRIORITY, nullptr, 1);
  return (result == pdPASS)
             ? Ok<Unit, PlatformError>(Unit{})
             : Err<Unit, PlatformError>(PlatformError::TaskCreateFailed);
}

void VehicleControlPlatformEsp32::DelayUntilNextTick(uint32_t period_ms) {
  if (!wake_time_initialized_) {
    last_wake_time_ = xTaskGetTickCount();
    wake_time_initialized_ = true;
  }
  const TickType_t period_ticks = pdMS_TO_TICKS(period_ms);
  vTaskDelayUntil(&last_wake_time_, period_ticks ? period_ticks : 1);
}

// ─────────────────────────────────────────────────────────────────────────
// Watchdog
// ─────────────────────────────────────────────────────────────────────────

void VehicleControlPlatformEsp32::RegisterTaskWdt() {
  esp_err_t err = esp_task_wdt_add(nullptr);  // nullptr = текущая задача
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to register control task in WDT: %s",
             esp_err_to_name(err));
  } else {
    ESP_LOGI(TAG, "Control task registered in Task WDT");
  }
}

void VehicleControlPlatformEsp32::FeedTaskWdt() noexcept {
  esp_task_wdt_reset();
}

}  // namespace rc_vehicle
