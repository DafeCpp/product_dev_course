#include "vehicle_control_platform_esp32.hpp"

#include <cstdio>
#include <cstring>
#include <firmware_common/esp32/ws_telem_channel.hpp>

#include "config.hpp"
#include "control_components.hpp"  // rc_vehicle::TelemetrySnapshot, BuildTelemJson
#include "crash_logger.hpp"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "failsafe.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "imu.hpp"
#include "imu_calibration_nvs.hpp"
#include "mag.hpp"
#include "mag_calibration_nvs.hpp"
#include "pwm_control.hpp"
#include "rc_input.hpp"
#include "rc_vehicle_common.hpp"
#include "stabilization_config_nvs.hpp"

namespace rc_vehicle {

static const char* TAG = "platform_esp32";

// Константы для задачи control loop
static constexpr uint32_t CONTROL_TASK_STACK = 12288;
static constexpr UBaseType_t CONTROL_TASK_PRIORITY = configMAX_PRIORITIES - 1;

// Канал WS-телеметрии: единственный экземпляр на прошивку (один control
// loop — один продьюсер).
static firmware_common::esp32::WsTelemChannel<TelemetrySnapshot> g_ws_telem;

esp_err_t RcWsTelemStart() { return g_ws_telem.Start(&BuildTelemJson); }

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

std::expected<void, PlatformError> VehicleControlPlatformEsp32::InitPwm() {
  return (PwmControlInit() == 0)
             ? std::expected<void, PlatformError>{}
             : std::unexpected(PlatformError::PwmInitFailed);
}

std::expected<void, PlatformError> VehicleControlPlatformEsp32::InitRc() {
  return (RcInputInit() == 0) ? std::expected<void, PlatformError>{}
                              : std::unexpected(PlatformError::RcInitFailed);
}

std::expected<void, PlatformError> VehicleControlPlatformEsp32::InitImu() {
  return (ImuInit() == 0) ? std::expected<void, PlatformError>{}
                          : std::unexpected(PlatformError::ImuInitFailed);
}

std::expected<void, PlatformError> VehicleControlPlatformEsp32::InitFailsafe() {
  // Failsafe инициализируется в конструкторе
  return std::expected<void, PlatformError>{};
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

void VehicleControlPlatformEsp32::LogCoreLoad() const {
  // LOS-219/250: узнать, простаивает ли ядро с веб-стеком (httpd/WS без
  // core-affinity, WiFi driver task пиннен на core 0), пока control-таск
  // (core 1, макс. приоритет) перегружен. Нужны CONFIG_FREERTOS_USE_
  // TRACE_FACILITY и CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS (sdkconfig.
  // defaults). ulRunTimeCounter кумулятивен с загрузки, не скользящее окно —
  // достаточно для диагностической сессии в несколько минут.
  constexpr UBaseType_t kMaxTasks = 24;
  static TaskStatus_t
      task_status[kMaxTasks];  // static — не в стеке control-таска
  configRUN_TIME_COUNTER_TYPE total_run_time = 0;
  const UBaseType_t num_tasks =
      uxTaskGetSystemState(task_status, kMaxTasks, &total_run_time);
  if (total_run_time == 0) return;

  // total_run_time на SMP (2 ядра) — сумма runtime-счётчиков ВСЕХ задач на
  // ОБОИХ ядрах (оба всегда что-то исполняют, включая собственный IDLE),
  // т.е. ~2x "настенного" времени интервала. Нормируем на
  // total_run_time/configNUMBER_OF_CORES — стандартный приём для per-core
  // CPU% на ESP-IDF SMP FreeRTOS.
  const float per_core_total =
      static_cast<float>(total_run_time) / configNUMBER_OF_CORES;
  float idle_pct[configNUMBER_OF_CORES] = {};

  char buffer[400];
  int off = snprintf(buffer, sizeof(buffer), "CORE TASKS:");
  for (UBaseType_t i = 0;
       i < num_tasks && off > 0 && off < static_cast<int>(sizeof(buffer)) - 48;
       ++i) {
    const auto& t = task_status[i];
    const float pct =
        per_core_total > 0.f
            ? 100.f * static_cast<float>(t.ulRunTimeCounter) / per_core_total
            : 0.f;
    const bool pinned = (t.xCoreID == 0 || t.xCoreID == 1);
    if (pinned && strncmp(t.pcTaskName, "IDLE", 4) == 0) {
      idle_pct[t.xCoreID] = pct;
    }
    off += snprintf(buffer + off, sizeof(buffer) - off, " %s(c%s)=%.1f%%",
                    t.pcTaskName, pinned ? (t.xCoreID == 0 ? "0" : "1") : "?",
                    pct);
  }
  ESP_LOGI(TAG, "%s", buffer);

  char summary[96];
  int soff = 0;
  for (int c = 0; c < configNUMBER_OF_CORES; ++c) {
    soff += snprintf(summary + soff, sizeof(summary) - soff,
                     "core%d_busy=%.1f%% ", c, 100.f - idle_pct[c]);
  }
  ESP_LOGI(TAG, "CORE LOAD: %s", summary);
}

// ─────────────────────────────────────────────────────────────────────────
// IMU
// ─────────────────────────────────────────────────────────────────────────

std::optional<ImuData> VehicleControlPlatformEsp32::ReadImu() {
  ImuData data{};
  if (ImuRead(data) == 0) {
    NormalizeMountedImuToVehicleFrame(data);
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

bool VehicleControlPlatformEsp32::InitMag() { return MagInit() == 0; }

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

std::expected<void, PlatformError> VehicleControlPlatformEsp32::SaveCalib(
    const ImuCalibData& data) {
  return (imu_nvs::Save(data) == ESP_OK)
             ? std::expected<void, PlatformError>{}
             : std::unexpected(PlatformError::CalibSaveFailed);
}

std::expected<void, PlatformError> VehicleControlPlatformEsp32::SaveComOffset(
    const float offset[2]) {
  return (imu_nvs::SaveComOffset(offset) == ESP_OK)
             ? std::expected<void, PlatformError>{}
             : std::unexpected(PlatformError::CalibSaveFailed);
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

std::optional<StabilizationConfig>
VehicleControlPlatformEsp32::LoadStabilizationConfig(DriveMode mode) {
  StabilizationConfig config{};
  if (stab_config_nvs::Load(mode, config) == ESP_OK && config.IsValid()) {
    return config;
  }
  return std::nullopt;
}

std::expected<void, PlatformError>
VehicleControlPlatformEsp32::SaveStabilizationConfig(
    const StabilizationConfig& config) {
  return (stab_config_nvs::Save(config) == ESP_OK)
             ? std::expected<void, PlatformError>{}
             : std::unexpected(PlatformError::CalibSaveFailed);
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
  return firmware_common::esp32::WebSocketGetClientCount();
}

void VehicleControlPlatformEsp32::PublishTelem(const TelemetrySnapshot& snap) {
  // FW-RF8: из control loop — только публикация POD-снимка в очередь (memcpy,
  // без аллокаций). Построение JSON и отправку по WS делает telem-задача.
  g_ws_telem.Enqueue(snap);
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

std::expected<void, PlatformError> VehicleControlPlatformEsp32::CreateTask(
    void (*entry)(void*), void* arg) {
  BaseType_t result =
      xTaskCreatePinnedToCore(entry, "vehicle_ctrl", CONTROL_TASK_STACK, arg,
                              CONTROL_TASK_PRIORITY, nullptr, 1);
  return (result == pdPASS) ? std::expected<void, PlatformError>{}
                            : std::unexpected(PlatformError::TaskCreateFailed);
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
  CrashLoggerTick(static_cast<uint32_t>(esp_timer_get_time() / 1000));
}

}  // namespace rc_vehicle
