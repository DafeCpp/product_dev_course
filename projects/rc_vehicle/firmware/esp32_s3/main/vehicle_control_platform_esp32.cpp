#include "vehicle_control_platform_esp32.hpp"

#include <cstring>

#include "config.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "failsafe.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "imu.hpp"
#include "imu_calibration_nvs.hpp"
#include "pwm_control.hpp"
#include "rc_input.hpp"
#include "rc_vehicle_common.hpp"
#include "websocket_server.hpp"

namespace rc_vehicle {

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

PlatformError VehicleControlPlatformEsp32::InitPwm() {
  return (PwmControlInit() == 0) ? PlatformError::Ok
                                 : PlatformError::PwmInitFailed;
}

PlatformError VehicleControlPlatformEsp32::InitRc() {
  return (RcInputInit() == 0) ? PlatformError::Ok : PlatformError::RcInitFailed;
}

PlatformError VehicleControlPlatformEsp32::InitImu() {
  return (ImuInit() == 0) ? PlatformError::Ok : PlatformError::ImuInitFailed;
}

PlatformError VehicleControlPlatformEsp32::InitFailsafe() {
  // Failsafe инициализируется в конструкторе
  return PlatformError::Ok;
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
// Калибровка IMU
// ─────────────────────────────────────────────────────────────────────────

std::optional<ImuCalibData> VehicleControlPlatformEsp32::LoadCalib() {
  ImuCalibData data{};
  if (imu_nvs::Load(data) == ESP_OK && data.valid) {
    return data;
  }
  return std::nullopt;
}

bool VehicleControlPlatformEsp32::SaveCalib(const ImuCalibData& data) {
  return imu_nvs::Save(data) == ESP_OK;
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
  // WebSocketSendTelem ожидает null-terminated строку
  char buffer[1024];
  size_t len = std::min(json.size(), sizeof(buffer) - 1);
  std::memcpy(buffer, json.data(), len);
  buffer[len] = '\0';
  WebSocketSendTelem(buffer);
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

bool VehicleControlPlatformEsp32::CreateTask(void (*entry)(void*), void* arg) {
  BaseType_t result =
      xTaskCreatePinnedToCore(entry, "vehicle_ctrl", CONTROL_TASK_STACK, arg,
                              CONTROL_TASK_PRIORITY, nullptr, 1);
  return result == pdPASS;
}

void VehicleControlPlatformEsp32::DelayUntilNextTick(uint32_t period_ms) {
  static TickType_t last_wake_time = xTaskGetTickCount();
  const TickType_t period_ticks = pdMS_TO_TICKS(period_ms);
  vTaskDelayUntil(&last_wake_time, period_ticks ? period_ticks : 1);
}

}  // namespace rc_vehicle
