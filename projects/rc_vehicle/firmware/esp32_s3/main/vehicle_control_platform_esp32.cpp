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
  //
  // Не используем TaskStatus_t::xCoreID — его наличие в структуре зависит
  // от configTASKLIST_INCLUDE_COREID / CONFIG_FREERTOS_VTASKLIST_INCLUDE_
  // COREID, а на некоторых версиях/конфигурациях ESP-IDF (SMP-ядро) это
  // поле в структуре отсутствует вовсе — компиляция падает. Вместо этого
  // ищем задачи с именами "IDLE0"/"IDLE1": эта нумерация — часть самого
  // FreeRTOS-Kernel (tasks.c, prvCreateIdleTasks(), не Kconfig-опция) и
  // одинакова на всех версиях с configNUMBER_OF_CORES > 1.
  // Код-ревью PR #297: uxTaskGetSystemState() не отдаёт частичный результат
  // и total_run_time при массиве меньше текущего числа задач (вернёт 0
  // задач) — молча теряем диагностику именно на полностью загруженной
  // системе (WiFi+HTTP+DNS+WS+UDP+control+...), ради которой она и нужна.
  // Проверяем живое число задач и явно предупреждаем, если не влезаем,
  // вместо тихого no-op.
  constexpr UBaseType_t kMaxTasks = 32;
  static TaskStatus_t
      task_status[kMaxTasks];  // static — не в стеке control-таска
  const UBaseType_t live_task_count = uxTaskGetNumberOfTasks();
  if (live_task_count > kMaxTasks) {
    ESP_LOGW(TAG, "LogCoreLoad: %u tasks > kMaxTasks=%u, пропущено",
             static_cast<unsigned>(live_task_count),
             static_cast<unsigned>(kMaxTasks));
    return;
  }

  configRUN_TIME_COUNTER_TYPE total_run_time = 0;
  const UBaseType_t num_tasks =
      uxTaskGetSystemState(task_status, kMaxTasks, &total_run_time);
  if (total_run_time == 0) return;

  // Код-ревью PR #297: pulTotalRunTime у uxTaskGetSystemState() —
  // ЕДИНЫЙ общий счётчик (*pulTotalRunTime = portGET_RUN_TIME_COUNTER_
  // VALUE(); см. FreeRTOS-Kernel/tasks.c), а НЕ сумма per-task счётчиков
  // по обоим ядрам. Задача исполняется только на ОДНОМ ядре одновременно,
  // поэтому её доля от total_run_time — это уже её доля от общего
  // таймлайна; деление на configNUMBER_OF_CORES было ошибкой и удваивало
  // все проценты (IDLE, занимающий 100% своего ядра, показывал бы 200%,
  // busy% уходил в -100%).
  float idle_pct[2] = {-1.f, -1.f};  // -1 = не найдено (< 2 ядер/имя другое)

  // Код-ревью PR #297: сканируем ВСЕ num_tasks на IDLE0/IDLE1 независимо
  // от заполненности текстового буфера — иначе на системе с большим
  // числом задач переполнение buffer[] могло прервать цикл ДО того, как
  // встретится IDLE-задача, и её % терялся бы молча (не просто не попадал
  // бы в текстовую строку). Буфер ограничивает только вывод текста.
  char buffer[400];
  int off = snprintf(buffer, sizeof(buffer), "CORE TASKS:");
  for (UBaseType_t i = 0; i < num_tasks; ++i) {
    const auto& t = task_status[i];
    const float pct = 100.f * static_cast<float>(t.ulRunTimeCounter) /
                      static_cast<float>(total_run_time);
    if (strcmp(t.pcTaskName, "IDLE0") == 0) {
      idle_pct[0] = pct;
    } else if (strcmp(t.pcTaskName, "IDLE1") == 0) {
      idle_pct[1] = pct;
    }
    if (off > 0 && off < static_cast<int>(sizeof(buffer)) - 32) {
      off += snprintf(buffer + off, sizeof(buffer) - off, " %s=%.1f%%",
                      t.pcTaskName, pct);
    }
  }
  ESP_LOGI(TAG, "%s", buffer);

  char summary[96];
  int soff = 0;
  for (int c = 0; c < 2; ++c) {
    if (idle_pct[c] < 0.f) continue;  // "IDLEc" не найден — не печатаем
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
