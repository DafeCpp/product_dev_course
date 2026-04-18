#pragma once

#include <algorithm>

namespace rc_vehicle {

/**
 * @brief Дискретный ПИД-регулятор с anti-windup и ограничением выхода
 *
 * Используется для замкнутого контура управления угловой скоростью по оси Z
 * (yaw rate). Вычисляет поправку управляющего сигнала по формуле:
 *
 *   u = Kp·e + Ki·∫e·dt + Kd·de/dt
 *
 * Особенности:
 * - На первом шаге D-составляющая равна 0 (нет истории)
 * - Интегратор ограничивается max_integral (anti-windup)
 * - Выход ограничивается max_output
 * - dt ≤ 0 → возвращает 0, состояние не меняется
 */
class PidController {
 public:
  /**
   * @brief Коэффициенты и ограничения ПИД
   */
  struct Gains {
    float kp{0.0f};           ///< Пропорциональный коэффициент
    float ki{0.0f};           ///< Интегральный коэффициент
    float kd{0.0f};           ///< Дифференциальный коэффициент
    float max_integral{1.0f}; ///< Anti-windup: ограничение накопителя
    float max_output{1.0f};   ///< Ограничение выходного значения
  };

  PidController() = default;
  explicit PidController(const Gains& gains) : gains_(gains) {}

  /**
   * @brief Установить коэффициенты регулятора
   * @param gains Новые коэффициенты
   */
  void SetGains(const Gains& gains) noexcept { gains_ = gains; }

  /**
   * @brief Получить текущие коэффициенты
   * @return Коэффициенты
   */
  [[nodiscard]] const Gains& GetGains() const noexcept { return gains_; }

  /**
   * @brief Выполнить один шаг регулятора
   * @param error Ошибка (desired - actual)
   * @param dt_sec Шаг времени в секундах (должен быть > 0)
   * @return Управляющий сигнал, ограниченный max_output
   */
  float Step(float error, float dt_sec) noexcept;

  /**
   * @brief Сбросить интегратор и историю производной
   */
  void Reset() noexcept;

  /**
   * @brief Получить текущее значение интегратора
   * @return Накопленный интеграл
   */
  [[nodiscard]] float GetIntegral() const noexcept { return integral_; }

 private:
  Gains gains_{};
  float integral_{0.0f};
  float prev_error_{0.0f};
  bool first_step_{true};
};

}  // namespace rc_vehicle
