#pragma once

#include <cstdint>

namespace bench {

/**
 * @brief Накопитель статистики периода тика (джиттер контура)
 *
 * Гистограмма с фиксированными бинами — без кучи и без печати в hot
 * path; отчёт (min/avg/max/p99) считается по запросу. Используется и
 * на хосте, и на ESP32 (60-секундная измерительная сессия).
 */
class TickStats {
 public:
  static constexpr uint32_t kBinWidthUs = 10;
  static constexpr uint32_t kBinCount = 400;  // 0..4000 мкс

  /// Зафиксировать момент тика (первый вызов только запоминает время)
  void Record(uint64_t now_us) noexcept {
    if (has_prev_) {
      const uint64_t delta = now_us - prev_us_;
      const auto d = static_cast<uint32_t>(delta);
      if (count_ == 0 || d < min_us_) min_us_ = d;
      if (count_ == 0 || d > max_us_) max_us_ = d;
      sum_us_ += delta;
      ++count_;
      uint32_t bin = d / kBinWidthUs;
      if (bin >= kBinCount) bin = kBinCount - 1;
      ++bins_[bin];
    }
    prev_us_ = now_us;
    has_prev_ = true;
  }

  struct Report {
    uint32_t count{0};
    uint32_t min_us{0};
    uint32_t max_us{0};
    uint32_t avg_us{0};
    uint32_t p99_us{0};
  };

  [[nodiscard]] Report MakeReport() const noexcept {
    Report r{};
    r.count = count_;
    if (count_ == 0) return r;
    r.min_us = min_us_;
    r.max_us = max_us_;
    r.avg_us = static_cast<uint32_t>(sum_us_ / count_);
    r.p99_us = Percentile(99);
    return r;
  }

  void Reset() noexcept {
    for (uint32_t& b : bins_) b = 0;
    count_ = 0;
    sum_us_ = 0;
    min_us_ = 0;
    max_us_ = 0;
    has_prev_ = false;
  }

 private:
  [[nodiscard]] uint32_t Percentile(uint32_t pct) const noexcept {
    const uint64_t threshold = (static_cast<uint64_t>(count_) * pct + 99) / 100;
    uint64_t seen = 0;
    for (uint32_t i = 0; i < kBinCount; ++i) {
      seen += bins_[i];
      if (seen >= threshold) {
        return (i + 1) * kBinWidthUs;  // верхняя граница бина
      }
    }
    return max_us_;
  }

  uint32_t bins_[kBinCount]{};
  uint32_t count_{0};
  uint64_t sum_us_{0};
  uint32_t min_us_{0};
  uint32_t max_us_{0};
  uint64_t prev_us_{0};
  bool has_prev_{false};
};

}  // namespace bench
