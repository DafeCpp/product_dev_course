#pragma once

#include <cstdio>

#include "bench_platform.hpp"

namespace bench {

/**
 * @brief Платформа с логическим временем для тестов и SIL
 *
 * Паттерн StdioPlatform из RC Vehicle: время продвигает вызывающая
 * сторона (AdvanceTimeMs) перед каждым HostStep; DelayUntilNextTick —
 * no-op, задача не создаётся (контур гоняется через HostStep).
 */
class HostPlatform final : public BenchPlatform {
 public:
  [[nodiscard]] uint32_t GetTimeMs() const noexcept override {
    return static_cast<uint32_t>(time_us_ / 1000);
  }

  [[nodiscard]] uint64_t GetTimeUs() const noexcept override {
    return time_us_;
  }

  void Log(LogLevel level, std::string_view msg) const override {
    static constexpr const char* kNames[] = {"INFO", "WARN", "ERROR"};
    std::fprintf(stderr, "[%s] %.*s\n", kNames[static_cast<int>(level)],
                 static_cast<int>(msg.size()), msg.data());
  }

  [[nodiscard]] std::expected<void, PlatformError> CreateControlTask(
      void (*entry)(void*), void* arg) override {
    (void)entry;
    (void)arg;
    return {};  // Хост гоняет контур через HostStep — задача не нужна
  }

  void DelayUntilNextTick(uint32_t period_ms) override { (void)period_ms; }

  void AdvanceTimeMs(uint32_t dt_ms) noexcept {
    time_us_ += static_cast<uint64_t>(dt_ms) * 1000;
  }

 private:
  uint64_t time_us_{0};
};

}  // namespace bench
