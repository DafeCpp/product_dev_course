#pragma once

#include <cstddef>
#include <cstdint>

namespace bench {

/**
 * @brief Тип события журнала
 *
 * Значения arg0..arg2 зависят от типа:
 * - kCommand:      arg0 = CommandType, arg1 = CommandResult,
 *                  arg2 = RejectReason, flags bit0 = duplicate
 * - kLinkState:    arg0 = прежний LinkState, arg1 = новый LinkState
 * - kFailureLatch: латч детектора разрушения образца
 * - kHold:         arg0 = StopReason (причина удержания)
 */
enum class JournalKind : uint8_t {
  kCommand = 0,
  kLinkState,
  kFailureLatch,
  kHold,
};

/// Запись журнала — POD, фиксированный размер
struct JournalEntry {
  uint32_t seq{0};     ///< Сквозной номер, растёт с 0
  uint32_t t_ms{0};    ///< Время с момента старта
  uint32_t cmd_id{0};  ///< Только для kCommand
  JournalKind kind{JournalKind::kCommand};
  uint8_t arg0{0};
  uint8_t arg1{0};
  uint8_t arg2{0};
  uint8_t flags{0};
};

/**
 * @brief Кольцевой журнал событий (ручные вмешательства, переходы, латчи)
 *
 * Принадлежит контуру и не потокобезопасен: наружу журнал уходит
 * хвостом в BenchStateSnapshot через ISnapshotPublisher. Полный
 * audit-протокол — LOS-74, здесь только локальный буфер.
 */
class EventJournal {
 public:
  static constexpr size_t kCapacity = 64;

  void Append(JournalEntry entry) noexcept {
    entry.seq = total_;
    entries_[total_ % kCapacity] = entry;
    ++total_;
  }

  /// Сколько событий записано за всё время (включая вытесненные)
  [[nodiscard]] uint32_t Total() const noexcept { return total_; }

  /**
   * @brief Скопировать последние события в out (от старых к новым)
   * @return Число записанных элементов
   */
  size_t CopyTail(JournalEntry* out, size_t max) const noexcept {
    size_t n = total_ < kCapacity ? total_ : kCapacity;
    if (n > max) n = max;
    const uint32_t first = total_ - static_cast<uint32_t>(n);
    for (size_t i = 0; i < n; ++i) {
      out[i] = entries_[(first + i) % kCapacity];
    }
    return n;
  }

 private:
  JournalEntry entries_[kCapacity]{};
  uint32_t total_{0};
};

}  // namespace bench
