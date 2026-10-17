#pragma once

#include <cstddef>
#include <cstdint>

#include "bench_platform.hpp"
#include "bench_state.hpp"

namespace bench {

/**
 * @brief Спул журнала в энергонезависимую память через хук платформы
 *
 * Работает в низкоприоритетной задаче поверх опубликованных снимков:
 * берёт новые записи из хвоста журнала (seq больше уже сохранённого) и
 * отдаёт их BenchPlatform::PersistJournalEntry. Контур 500 Гц flash не
 * трогает. Если между вызовами накопилось больше kJournalTail записей,
 * старшие теряются — для отладочного буфера это приемлемо (полный
 * audit — LOS-74).
 */
class JournalSpool {
 public:
  /// @return сколько записей сохранено в этом вызове
  size_t Spool(const BenchStateSnapshot& snapshot, BenchPlatform& platform) {
    size_t saved = 0;
    for (size_t i = 0; i < snapshot.journal_count; ++i) {
      const JournalEntry& e = snapshot.journal[i];
      if (has_last_ && e.seq <= last_seq_) continue;
      if (!platform.PersistJournalEntry(e))
        break;  // повторим при следующем вызове
      last_seq_ = e.seq;
      has_last_ = true;
      ++saved;
    }
    return saved;
  }

 private:
  uint32_t last_seq_{0};
  bool has_last_{false};
};

}  // namespace bench
