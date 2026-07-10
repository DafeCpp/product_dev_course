#pragma once

#include <algorithm>

namespace bench {

/**
 * @brief Дискретный ПИД-регулятор с anti-windup и ограничением выхода
 *
 * Копия rc_vehicle::PidController (см. projects/rc_vehicle/firmware) с
 * добавленным SetIntegral() для bumpless-переключения режимов: при смене
 * контура force↔displacement интегратор входящего регулятора
 * пре-загружается так, чтобы первый выход совпал с последней командой.
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
    float kp{0.0f};            ///< Пропорциональный коэффициент
    float ki{0.0f};            ///< Интегральный коэффициент
    float kd{0.0f};            ///< Дифференциальный коэффициент
    float max_integral{1.0f};  ///< Anti-windup: ограничение накопителя
    float max_output{1.0f};    ///< Ограничение выходного значения
  };

  PidController() = default;
  explicit PidController(const Gains& gains) : gains_(gains) {}

  void SetGains(const Gains& gains) noexcept { gains_ = gains; }
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
   * @brief Принудительно установить интегратор (bumpless-переключение)
   * @param integral Значение накопителя (клампится max_integral)
   */
  void SetIntegral(float integral) noexcept {
    integral_ = std::clamp(integral, -gains_.max_integral, gains_.max_integral);
  }

  [[nodiscard]] float GetIntegral() const noexcept { return integral_; }

 private:
  Gains gains_{};
  float integral_{0.0f};
  float prev_error_{0.0f};
  bool first_step_{true};
};

}  // namespace bench
