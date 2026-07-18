#include "bench_platform_esp32.hpp"

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace bench {

namespace {
constexpr const char* kTag = "bench";
constexpr uint32_t kControlTaskStack = 8192;
constexpr UBaseType_t kControlTaskPriority = configMAX_PRIORITIES - 1;
constexpr BaseType_t kControlTaskCore = 1;  // ядро 0 — эмулятор/система
}  // namespace

uint32_t BenchPlatformEsp32::GetTimeMs() const noexcept {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

uint64_t BenchPlatformEsp32::GetTimeUs() const noexcept {
  return static_cast<uint64_t>(esp_timer_get_time());
}

void BenchPlatformEsp32::Log(LogLevel level, std::string_view msg) const {
  switch (level) {
    case LogLevel::Info:
      ESP_LOGI(kTag, "%.*s", static_cast<int>(msg.size()), msg.data());
      break;
    case LogLevel::Warning:
      ESP_LOGW(kTag, "%.*s", static_cast<int>(msg.size()), msg.data());
      break;
    case LogLevel::Error:
      ESP_LOGE(kTag, "%.*s", static_cast<int>(msg.size()), msg.data());
      break;
  }
}

std::expected<void, PlatformError> BenchPlatformEsp32::CreateControlTask(
    void (*entry)(void*), void* arg) {
  const BaseType_t result =
      xTaskCreatePinnedToCore(entry, "bench_ctrl", kControlTaskStack, arg,
                              kControlTaskPriority, nullptr, kControlTaskCore);
  return result == pdPASS ? std::expected<void, PlatformError>{}
                          : std::unexpected(PlatformError::TaskCreateFailed);
}

void BenchPlatformEsp32::DelayUntilNextTick(uint32_t period_ms) {
  if (!wake_time_initialized_) {
    last_wake_time_ = xTaskGetTickCount();
    wake_time_initialized_ = true;
  }
  const TickType_t period_ticks = pdMS_TO_TICKS(period_ms);
  vTaskDelayUntil(&last_wake_time_, period_ticks ? period_ticks : 1);
}

void BenchPlatformEsp32::RegisterTaskWdt() {
  const esp_err_t err = esp_task_wdt_add(nullptr);
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "task WDT add failed: %s", esp_err_to_name(err));
  }
}

void BenchPlatformEsp32::FeedTaskWdt() noexcept { esp_task_wdt_reset(); }

}  // namespace bench
