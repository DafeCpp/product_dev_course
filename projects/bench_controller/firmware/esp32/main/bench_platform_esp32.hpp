#pragma once

#include "bench_platform.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace bench {

/**
 * @brief BenchPlatform для ESP32-S3
 *
 * Контур управления — задача FreeRTOS, закреплённая за ядром 1 (ядро 0
 * отдано эмулятору клапана и системным задачам). Тик держится
 * vTaskDelayUntil'ом на фиксированной сетке; при CONFIG_FREERTOS_HZ=1000
 * период 2 мс = ровно 2 тика планировщика. Время — esp_timer (1 мкс),
 * им же считается джиттер. Watchdog — esp_task_wdt.
 *
 * Паттерн зеркалит VehicleControlPlatformEsp32 из RC Vehicle.
 */
class BenchPlatformEsp32 final : public BenchPlatform {
 public:
  [[nodiscard]] uint32_t GetTimeMs() const noexcept override;
  [[nodiscard]] uint64_t GetTimeUs() const noexcept override;
  void Log(LogLevel level, std::string_view msg) const override;

  [[nodiscard]] std::expected<void, PlatformError> CreateControlTask(
      void (*entry)(void*), void* arg) override;
  void DelayUntilNextTick(uint32_t period_ms) override;
  void FeedTaskWdt() noexcept override;

  /// Зарегистрировать текущую задачу в Task WDT (из тела задачи)
  void RegisterTaskWdt();

 private:
  TickType_t last_wake_time_{0};
  bool wake_time_initialized_{false};
};

}  // namespace bench
