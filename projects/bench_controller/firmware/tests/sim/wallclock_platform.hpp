#pragma once

#include <ctime>

#include "bench_platform.hpp"

namespace bench {

/**
 * @brief Платформа реального времени для измерительного рига (Linux)
 *
 * CLOCK_MONOTONIC + clock_nanosleep(TIMER_ABSTIME): DelayUntilNextTick
 * держит фиксированную сетку тиков (без накопления дрейфа) — тот же
 * контракт, что vTaskDelayUntil на ESP32. Используется sim_host
 * --socketcan; в CI не участвует.
 */
class WallclockPlatform final : public BenchPlatform {
 public:
  WallclockPlatform() {
    clock_gettime(CLOCK_MONOTONIC, &start_);
    next_tick_ = start_;
  }

  [[nodiscard]] uint32_t GetTimeMs() const noexcept override {
    return static_cast<uint32_t>(NowUs() / 1000);
  }

  [[nodiscard]] uint64_t GetTimeUs() const noexcept override { return NowUs(); }

  void Log(LogLevel level, std::string_view msg) const override {
    static constexpr const char* kNames[] = {"INFO", "WARN", "ERROR"};
    std::fprintf(stderr, "[%s] %.*s\n", kNames[static_cast<int>(level)],
                 static_cast<int>(msg.size()), msg.data());
  }

  [[nodiscard]] std::expected<void, PlatformError> CreateControlTask(
      void (*entry)(void*), void* arg) override {
    (void)entry;
    (void)arg;
    return {};  // риг гоняет контур через HostStep с внешним пейсингом
  }

  void DelayUntilNextTick(uint32_t period_ms) override {
    next_tick_.tv_nsec += static_cast<long>(period_ms) * 1'000'000L;
    while (next_tick_.tv_nsec >= 1'000'000'000L) {
      next_tick_.tv_nsec -= 1'000'000'000L;
      ++next_tick_.tv_sec;
    }
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_tick_, nullptr);
  }

 private:
  [[nodiscard]] uint64_t NowUs() const noexcept {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    const int64_t ns =
        (static_cast<int64_t>(now.tv_sec) - start_.tv_sec) * 1'000'000'000LL +
        (static_cast<int64_t>(now.tv_nsec) - start_.tv_nsec);
    return static_cast<uint64_t>(ns / 1000);
  }

  timespec start_{};
  timespec next_tick_{};
};

}  // namespace bench
