#pragma once

#include <cstdint>

#include "bench_types.hpp"
#include "ramp_generator.hpp"

namespace bench {

/**
 * @brief Наблюдатель связи с supervisory-уровнем
 *
 * Политика по итогам discovery (LOS-67): MCU не продолжает программу
 * автономно. Состояния:
 *
 *   Running → (связь потеряна) → GracePeriod → (grace истёк) →
 *   RampDown → (рампа завершена) → SafeHold
 *
 * Восстановление связи возвращает в Running ТОЛЬКО из GracePeriod:
 * начатая разгрузка доводится до конца, возобновление теста — по
 * явной команде оператора (вне рамок спайка — Reset()).
 */
class LinkWatchdog {
 public:
  struct Config {
    uint32_t grace_ms{500};   ///< Ожидание восстановления связи
    uint32_t ramp_ms{2000};   ///< Длительность плавной разгрузки
  };

  LinkWatchdog() = default;
  explicit LinkWatchdog(const Config& config) : config_(config) {}

  /**
   * @brief Шаг наблюдателя (каждый тик)
   * @param now_ms Текущее время
   * @param link_alive Связь с платформой жива
   * @return Текущее состояние
   */
  LinkState Update(uint32_t now_ms, bool link_alive) noexcept;

  [[nodiscard]] LinkState State() const noexcept { return state_; }

  /// Масштаб амплитуды программы: 1 (Running/Grace) → 0 (SafeHold)
  [[nodiscard]] float RampScale(uint32_t now_ms) const noexcept {
    if (state_ == LinkState::kSafeHold) return 0.0f;
    if (state_ == LinkState::kRampDown) return ramp_.Scale(now_ms);
    return 1.0f;
  }

  /// Вернуться в Running (явная команда оператора после восстановления)
  void Reset() noexcept {
    state_ = LinkState::kRunning;
    ramp_.Reset();
  }

 private:
  Config config_{};
  LinkState state_{LinkState::kRunning};
  uint32_t link_lost_at_ms_{0};
  RampGenerator ramp_{};
};

}  // namespace bench
