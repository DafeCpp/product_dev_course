#pragma once

#include <cstdint>

namespace bench {

/**
 * @brief Линейная рампа масштаба 1 → 0 за заданное время
 *
 * Используется LinkWatchdog для плавной разгрузки: масштаб умножается
 * на амплитудную часть программы (значение стягивается к среднему).
 */
class RampGenerator {
 public:
  /// Запустить рампу с текущего момента
  void Start(uint32_t now_ms, uint32_t duration_ms) noexcept {
    start_ms_ = now_ms;
    duration_ms_ = duration_ms;
    active_ = true;
  }

  /// Масштаб [1..0]; 1 до старта, 0 после завершения
  [[nodiscard]] float Scale(uint32_t now_ms) const noexcept {
    if (!active_) return 1.0f;
    if (duration_ms_ == 0) return 0.0f;
    uint32_t elapsed = now_ms - start_ms_;
    if (elapsed >= duration_ms_) return 0.0f;
    return 1.0f - static_cast<float>(elapsed) /
                      static_cast<float>(duration_ms_);
  }

  /// Рампа завершена (масштаб достиг 0)
  [[nodiscard]] bool Done(uint32_t now_ms) const noexcept {
    return active_ && (now_ms - start_ms_) >= duration_ms_;
  }

  void Reset() noexcept { active_ = false; }

 private:
  uint32_t start_ms_{0};
  uint32_t duration_ms_{0};
  bool active_{false};
};

}  // namespace bench
