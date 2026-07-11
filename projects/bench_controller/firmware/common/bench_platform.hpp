#pragma once

#include <cstdint>
#include <expected>
#include <string_view>

namespace bench {

/**
 * @brief Ошибки платформы
 */
enum class PlatformError : uint8_t {
  Ok = 0,
  TaskCreateFailed,
};

/**
 * @brief Уровни логирования
 */
enum class LogLevel : uint8_t { Info = 0, Warning, Error };

/**
 * @brief HAL контроллера стенда — минимальный платформенный контракт
 *
 * По образцу VehicleControlPlatform из RC Vehicle, но урезан до
 * времени/лога/планирования тика: CAN и связь с платформой живут за
 * отдельными интерфейсами (IValveChannel, ISupervisoryLink) и в HAL
 * не входят.
 *
 * Реализации: BenchPlatformEsp32 (FreeRTOS-задача + vTaskDelayUntil),
 * HostPlatform (логическое время для тестов и SIL).
 */
class BenchPlatform {
 public:
  virtual ~BenchPlatform() = default;

  /// Время с момента старта, мс
  [[nodiscard]] virtual uint32_t GetTimeMs() const noexcept = 0;

  /// Время с момента старта, мкс (для измерений джиттера)
  [[nodiscard]] virtual uint64_t GetTimeUs() const noexcept = 0;

  /// Логирование вне hot path
  virtual void Log(LogLevel level, std::string_view msg) const = 0;

  /**
   * @brief Создать задачу управляющего контура
   * @param entry Точка входа задачи
   * @param arg Аргумент, передаваемый в entry
   */
  [[nodiscard]] virtual std::expected<void, PlatformError> CreateControlTask(
      void (*entry)(void*), void* arg) = 0;

  /**
   * @brief Заснуть до следующего тика фиксированного периода
   * @param period_ms Период тика (2 мс для контура 500 Гц)
   */
  virtual void DelayUntilNextTick(uint32_t period_ms) = 0;

  /// Зарегистрировать задачу контура в task watchdog (из тела задачи,
  /// один раз перед циклом). No-op по умолчанию; на ESP32 обязателен
  /// перед FeedTaskWdt() (esp_task_wdt_add до esp_task_wdt_reset).
  virtual void RegisterTaskWdt() {}

  /// Сброс task watchdog (no-op по умолчанию)
  virtual void FeedTaskWdt() noexcept {}
};

}  // namespace bench
