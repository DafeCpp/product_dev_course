#pragma once

#include <atomic>
#include <cstdint>
#include <expected>
#include <span>

#include "bench_command.hpp"
#include "bench_platform.hpp"
#include "bench_state.hpp"
#include "bench_types.hpp"
#include "channel_controller.hpp"
#include "event_journal.hpp"
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
  uint8_t valve_status{0};  ///< Статусное слово узла
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
 * Порядок тика: PreTick транспорта → чтение feedback → команда
 * оператора из очереди → safety (разрушение, связь) → программа с
 * рампой разгрузки либо ручная уставка → регулятор → запись setpoint →
 * PostTick транспорта → публикация снимка (раз в N тиков).
 *
 * Командный тракт (Ф2, LOS-185): команды приходят через
 * ICommandInbox, исполняются в начале тика идемпотентно по cmd_id и
 * журналируются; ручные команды не исполняются поверх идущей
 * программы. Журнал и снимок состояния уходят наружу только копией
 * через ISnapshotPublisher — никаких аллокаций и JSON в hot path.
 */
class BenchControlLoop {
 public:
  struct Config {
    uint32_t period_ms{2};  ///< 500 Гц
    ChannelController::Config controller{};
    SpecimenFailureDetector::Config failure{};
    LinkWatchdog::Config watchdog{};

    /// Ручное управление (команды set_setpoint)
    struct Manual {
      float force_limit_n{PdoScaling::kFullScaleForceN};  ///< |уставка| ≤
      float position_limit_mm{PdoScaling::kFullScalePositionMm};
      float force_ramp_n_per_s{5'000.0f};  ///< Рампа уставки по усилию
      float position_ramp_mm_per_s{5.0f};  ///< Рампа уставки по перемещению
    } manual{};

    uint32_t publish_every_ticks{20};   ///< 25 Гц при 500 Гц контуре
    uint32_t stats_refresh_ticks{500};  ///< Пересчёт отчёта джиттера
  };

  BenchControlLoop(const Config& config, BenchPlatform& platform,
                   IValveChannel& valve, ISupervisoryLink& supervisory);

  /// Загрузить программу нагружения (force-режим)
  void SetProgram(std::span<const SineProgram::Segment> segments);

  /// Подключить очередь команд (до Init()/первого HostStep)
  void AttachCommandInbox(ICommandInbox* inbox) noexcept { inbox_ = inbox; }

  /// Подключить приёмник снимков состояния (до Init()/первого HostStep)
  void AttachSnapshotPublisher(ISnapshotPublisher* publisher) noexcept {
    publisher_ = publisher;
  }

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

  enum class HoldReason : uint8_t { kNone = 0, kOperator, kLink, kFailure };

  void ProcessCommand(uint32_t now_ms, const BenchCommand& cmd,
                      const ValveFeedback& fb);
  [[nodiscard]] CommandAck Execute(uint32_t now_ms, const BenchCommand& cmd,
                                   const ValveFeedback& fb);
  void EnterHold(uint32_t now_ms, HoldReason hold, StopReason reason,
                 const ValveFeedback& fb);
  void RecordAck(const CommandAck& ack);
  void Log(JournalEntry entry, uint32_t now_ms);
  void Publish(uint32_t now_ms, const TickSnapshot& tick);

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
  bool holding_{false};           ///< Останов, разрушение или SafeHold
  HoldReason hold_reason_{HoldReason::kNone};
  StopReason last_stop_reason_{StopReason::kNone};

  // Командный тракт
  ICommandInbox* inbox_{nullptr};
  ISnapshotPublisher* publisher_{nullptr};
  EventJournal journal_{};
  static constexpr size_t kSeenCapacity = 16;
  CommandAck seen_[kSeenCapacity]{};  ///< Недавние cmd_id (кольцо)
  uint32_t seen_total_{0};
  CommandAck acks_[BenchStateSnapshot::kAckTail]{};
  uint32_t ack_total_{0};
  uint32_t cmd_total_{0};
  uint32_t cmd_rejected_{0};
  uint32_t cmd_duplicate_{0};

  // Ручная уставка (единицы активного режима)
  float manual_commanded_{0.0f};
  float manual_ramped_{0.0f};
  bool program_was_active_{false};
  LinkState prev_link_state_{LinkState::kRunning};
  bool prev_failure_latched_{false};

  // Публикация
  uint32_t tick_count_{0};
  uint32_t publish_seq_{0};
  TickStats::Report cached_stats_{};
  uint32_t last_loop_ms_{0};
  TickSnapshot snapshot_{};

  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> stopped_ack_{false};
};

}  // namespace bench
