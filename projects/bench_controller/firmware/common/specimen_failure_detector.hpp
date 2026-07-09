#pragma once

#include <cstdint>

namespace bench {

/**
 * @brief Детектор разрушения образца
 *
 * Разрушение в force-режиме опасно: сила падает, регулятор «догоняет»
 * уставку и разгоняет цилиндр. Детектор считает подряд идущие тики, в
 * которых фактическая сила ниже (1 - drop_fraction)·|уставка| при
 * значимой уставке (|уставка| ≥ min_force_n); после window_ticks —
 * латч (сбрасывается только Reset()). Тики с малой уставкой (проход
 * синуса через ноль) счётчик не сбрасывают и не наращивают.
 *
 * Взведение (arming): анализ провалов начинается только после
 * arm_ticks подряд тиков «в допуске» — иначе выход на режим в начале
 * теста (сила ещё догоняет рампу уставки) даёт ложный латч. Практика
 * реальных стендов: детекция разрыва активна после стабилизации.
 *
 * При контуре 500 Гц и window_ticks = 10 срабатывание занимает 20 мс —
 * заведомо меньше двух циклов процесса 10–50 Гц.
 */
class SpecimenFailureDetector {
 public:
  struct Config {
    float drop_fraction{0.3f};   ///< Порог падения: actual < (1-drop)·|sp|
    uint16_t window_ticks{10};   ///< Подряд идущих тиков до латча
    float min_force_n{1000.0f};  ///< Уставки ниже — не анализируем
    uint16_t arm_ticks{25};      ///< Тиков «в допуске» до взведения
  };

  SpecimenFailureDetector() = default;
  explicit SpecimenFailureDetector(const Config& config) : config_(config) {}

  /**
   * @brief Шаг детектора (вызывается каждый тик в force-режиме)
   * @param force_actual_n Фактическая сила
   * @param force_setpoint_n Текущая уставка силы
   * @return true, если разрушение зафиксировано (латч)
   */
  [[nodiscard]] bool Update(float force_actual_n,
                            float force_setpoint_n) noexcept;

  [[nodiscard]] bool Latched() const noexcept { return latched_; }
  [[nodiscard]] bool Armed() const noexcept { return armed_; }

  void Reset() noexcept {
    latched_ = false;
    armed_ = false;
    below_count_ = 0;
    in_track_count_ = 0;
  }

 private:
  Config config_{};
  uint16_t below_count_{0};
  uint16_t in_track_count_{0};
  bool armed_{false};
  bool latched_{false};
};

}  // namespace bench
