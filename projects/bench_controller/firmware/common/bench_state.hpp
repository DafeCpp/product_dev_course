#pragma once

#include <cstdint>
#include <mutex>

#include "bench_command.hpp"
#include "bench_platform.hpp"
#include "bench_types.hpp"
#include "event_journal.hpp"
#include "tick_stats.hpp"

namespace bench {

/// Причина последнего останова/удержания (P-4: «причина и инициатор»)
enum class StopReason : uint8_t {
  kNone = 0,
  kOperatorControlledStop,  ///< Команда controlled_stop (P-1)
  kOperatorEStopSoft,       ///< Команда e_stop_soft (P-2, программный)
  kLinkSafeHold,            ///< Потеря связи: grace → ramp → hold
  kSpecimenFailure,         ///< Латч детектора разрушения образца
};

/**
 * @brief POD-снимок состояния контура для UI/телеметрии
 *
 * Собирается контуром не на каждом тике, а раз в
 * Config::publish_every_ticks, и отдаётся через ISnapshotPublisher.
 * JSON строится из снимка вне hot path (snapshot_json.hpp); схема —
 * контракт для веб-страницы Ф4, см.
 * docs/experiment-control/bench-command-schema.md.
 */
struct BenchStateSnapshot {
  static constexpr size_t kAckTail = 4;
  static constexpr size_t kJournalTail = 16;

  uint32_t seq{0};  ///< Счётчик публикаций
  uint32_t uptime_ms{0};

  ControlMode mode{ControlMode::kDisplacement};
  LinkState link_state{LinkState::kRunning};
  bool enabled{true};
  bool holding{false};  ///< Удержание позиции после останова/защиты
  StopReason last_stop_reason{StopReason::kNone};
  bool program_active{false};
  bool failure_latched{false};
  EstopChainState estop_chain{EstopChainState::kUnknown};

  float program_target{0.0f};
  float manual_commanded{0.0f};  ///< Уставка из команды
  float manual_ramped{0.0f};     ///< Уставка после рампы
  float effective_target{0.0f};  ///< Цель после рампы захвата
  float valve_command{0.0f};

  float force_n{0.0f};
  float position_mm{0.0f};
  uint8_t valve_status{0};
  bool fb_fresh{false};
  uint32_t fb_age_ticks{0};

  uint32_t cmd_total{0};
  uint32_t cmd_rejected{0};
  uint32_t cmd_duplicate{0};
  uint32_t cmd_overwritten{0};

  CommandAck acks[kAckTail]{};  ///< Последние ack, от старых к новым
  uint8_t ack_count{0};

  TickStats::Report tick{};

  JournalEntry journal[kJournalTail]{};  ///< Хвост журнала, от старых к новым
  uint8_t journal_count{0};
  uint32_t journal_total{0};
};

/**
 * @brief Приёмник снимков состояния (контур → UI/линк)
 *
 * Вызывается из задачи контура раз в publish_every_ticks. Реализация
 * платформы — двойной буфер/очередь; LockedSnapshotExchange — для
 * host-тестов.
 */
class ISnapshotPublisher {
 public:
  virtual ~ISnapshotPublisher() = default;
  virtual void Publish(const BenchStateSnapshot& snapshot) = 0;
};

/// Последний снимок под мьютексом (host, тесты, mock)
class LockedSnapshotExchange final : public ISnapshotPublisher {
 public:
  void Publish(const BenchStateSnapshot& snapshot) override {
    std::lock_guard<std::mutex> lock(mutex_);
    latest_ = snapshot;
    ++published_;
  }

  [[nodiscard]] BenchStateSnapshot Latest() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return latest_;
  }

  [[nodiscard]] uint32_t PublishedCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return published_;
  }

 private:
  mutable std::mutex mutex_;
  BenchStateSnapshot latest_{};
  uint32_t published_{0};
};

}  // namespace bench
