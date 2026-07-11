#pragma once

#include <atomic>
#include <cstdint>
#include <expected>
#include <span>

#include "bench_platform.hpp"
#include "bench_types.hpp"
#include "channel_controller.hpp"
#include "link_watchdog.hpp"
#include "sine_program.hpp"
#include "specimen_failure_detector.hpp"
#include "supervisory_link.hpp"
#include "tick_stats.hpp"
#include "valve_channel.hpp"

namespace bench {

/**
 * @brief Срез состояния контура за тик (телеметрия, CSV, тесты)
 */
struct TickSnapshot {
  uint32_t now_ms{0};
  ControlMode mode{ControlMode::kDisplacement};
  LinkState link_state{LinkState::kRunning};
  float program_target{0.0f};    ///< Цель программы до рампы разгрузки
  float effective_target{0.0f};  ///< Цель после рампы захвата
  float valve_command{0.0f};     ///< Команда клапану [-1..1]
  float force_n{0.0f};
  float position_mm{0.0f};
  bool failure_latched{false};
  bool fb_fresh{false};      ///< Пришёл ли новый feedback в этом тике
  uint32_t fb_age_ticks{0};  ///< Возраст feedback в тиках
};

/**
 * @brief Управляющий контур канала нагружения (500 Гц)
 *
 * Владеет регулятором, программой, детектором разрушения и
 * наблюдателем связи; платформа, канал клапана и supervisory-линк
 * инжектируются по ссылке. Два способа запуска (паттерн
 * VehicleControlUnified из RC Vehicle):
 *
 * - Init() → задача платформы крутит ControlTaskLoop с
 *   DelayUntilNextTick(period);
 * - HostStep(dt_ms) → один тик с логическим временем (SIL/тесты).
 *
 * Порядок тика: PreTick транспорта → чтение feedback → safety
 * (разрушение, связь) → программа с рампой разгрузки → регулятор →
 * запись setpoint → PostTick транспорта.
 */
class BenchControlLoop {
 public:
  struct Config {
    uint32_t period_ms{2};  ///< 500 Гц
    ChannelController::Config controller{};
    SpecimenFailureDetector::Config failure{};
    LinkWatchdog::Config watchdog{};
  };

  BenchControlLoop(const Config& config, BenchPlatform& platform,
                   IValveChannel& valve, ISupervisoryLink& supervisory);

  /// Загрузить программу нагружения (force-режим)
  void SetProgram(std::span<const SineProgram::Segment> segments);

  /// Запустить задачу контура на платформе
  [[nodiscard]] std::expected<void, PlatformError> Init();

  /// Один тик с логическим временем (SIL): платформенное время
  /// продвигает вызывающая сторона
  TickSnapshot HostStep(uint32_t dt_ms);

  [[nodiscard]] const TickSnapshot& LastSnapshot() const noexcept {
    return snapshot_;
  }
  [[nodiscard]] const TickStats& Stats() const noexcept { return stats_; }
  [[nodiscard]] ChannelController& Controller() noexcept { return controller_; }

  /**
   * @brief Попросить задачу контура прекратить тики (для снятия
   *        статистики без гонки).
   *
   * Задача остаётся живой и кормит watchdog, но перестаёт вызывать
   * TickOnce — после Stopped() значения Stats()/feedback-счётчиков
   * никто конкурентно не меняет, их можно читать с другого ядра.
   */
  void RequestStop() noexcept { stop_requested_.store(true); }

  /// Тик-луп подтвердил остановку (TickOnce больше не вызывается)
  [[nodiscard]] bool Stopped() const noexcept { return stopped_ack_.load(); }

 private:
  static void ControlTaskEntry(void* arg);
  void ControlTaskLoop();
  void TickOnce(uint32_t now_ms, uint32_t dt_ms);

  Config config_{};
  BenchPlatform& platform_;
  IValveChannel& valve_;
  ISupervisoryLink& supervisory_;

  ChannelController controller_;
  SineProgram program_{};
  SpecimenFailureDetector failure_detector_;
  LinkWatchdog link_watchdog_;
  TickStats stats_{};

  float hold_position_mm_{0.0f};  ///< Цель displacement-hold
  bool holding_{false};           ///< Разрушение или SafeHold
  uint32_t last_loop_ms_{0};
  TickSnapshot snapshot_{};

  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> stopped_ack_{false};
};

}  // namespace bench
