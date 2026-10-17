#pragma once

#include <mutex>

#include "bench_command.hpp"

namespace bench {

/**
 * @brief Single-slot очередь команд с мьютексом (host, тесты, SIL)
 *
 * На целевой платформе заменяется FreeRTOS-реализацией без мьютекса в
 * hot path (xQueueOverwrite / xQueueReceive с нулевым ожиданием).
 */
class LockedCommandSlot final : public ICommandInbox {
 public:
  void Post(const BenchCommand& cmd) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (full_) ++overwritten_;
    slot_ = cmd;
    full_ = true;
  }

  [[nodiscard]] bool TryTake(BenchCommand& out) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!full_) return false;
    out = slot_;
    full_ = false;
    return true;
  }

  [[nodiscard]] uint32_t OverwrittenCount() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    return overwritten_;
  }

 private:
  mutable std::mutex mutex_;
  BenchCommand slot_{};
  bool full_{false};
  uint32_t overwritten_{0};
};

}  // namespace bench
