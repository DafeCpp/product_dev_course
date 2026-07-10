#pragma once

#include <cstdint>

#include "bench_types.hpp"
#include "pid_controller.hpp"

namespace bench {

/**
 * @brief Регулятор одного канала нагружения: force-PID + displacement-PID
 *        с безударным (bumpless) переключением режимов
 *
 * Переключение (по команде или по условию, например при детекции
 * разрушения) устроено так, чтобы команда клапану не имела скачка:
 *
 * 1. Цель входящего контура стартует с захваченного измерения
 *    (capture_value) и рампится к внешней цели за capture_ramp_s —
 *    ошибка в момент переключения ≈ 0.
 * 2. Интегратор входящего PID пре-загружается так, чтобы его первый
 *    выход совпал с последней командой: I = u_prev / ki (при e ≈ 0).
 *    Если ki == 0, вместо интегратора используется затухающий
 *    feed-forward u_prev → 0 за capture_ramp_s.
 * 3. Поверх всего — slew-rate лимит команды клапану.
 */
class ChannelController {
 public:
  struct Config {
    PidController::Gains force_gains{};
    PidController::Gains disp_gains{};
    /// Feed-forward по производной уставки: u_ff = ff · d(target)/dt.
    /// Для гидравлики ≈ 1/K_plant (обратный коэффициент передачи
    /// «команда → скорость изменения величины»); PID добирает остаток.
    /// Без FF чистый PI не отслеживает 10–50 Гц: полоса контура
    /// ограничена лагом золотника.
    float force_ff{0.0f};  ///< u на (Н/с)
    float disp_ff{0.0f};   ///< u на (мм/с)
    /// Lead-компенсация лага золотника (инверсная модель звена
    /// 1-го порядка): u_ff = ff · (v + τ·a), где v/a — первая/вторая
    /// производные цели. 0 — выключено. На 50 Гц без неё фазовое
    /// отставание τ = 8 мс даёт ~68° ошибки.
    float ff_lead_tau_s{0.0f};
    float output_slew_per_s{5.0f};  ///< Лимит скорости команды, 1/с
    float capture_ramp_s{0.5f};     ///< Рампа захвата цели при переключении
  };

  ChannelController() = default;
  explicit ChannelController(const Config& config);

  /**
   * @brief Запросить переключение режима (bumpless)
   * @param mode Новый режим
   * @param capture_value Захваченное измерение нового контура
   *        (сила в Н для kForce, позиция в мм для kDisplacement)
   *
   * Повторный запрос текущего режима игнорируется.
   */
  void RequestMode(ControlMode mode, float capture_value) noexcept;

  /**
   * @brief Один шаг регулятора
   * @param target Внешняя цель активного контура (Н или мм)
   * @param fb Обратная связь клапана
   * @param dt_sec Шаг времени
   * @return Команда клапану (mode + value [-1..1] + enable)
   */
  [[nodiscard]] ValveSetpoint Step(float target, const ValveFeedback& fb,
                                   float dt_sec) noexcept;

  [[nodiscard]] ControlMode Mode() const noexcept { return mode_; }

  /// Эффективная цель после рампы захвата (для телеметрии/тестов)
  [[nodiscard]] float EffectiveTarget() const noexcept {
    return effective_target_;
  }

  [[nodiscard]] float LastOutput() const noexcept { return last_output_; }

  void SetEnabled(bool enabled) noexcept { enabled_ = enabled; }

 private:
  [[nodiscard]] float Measured(const ValveFeedback& fb) const noexcept {
    return mode_ == ControlMode::kForce ? fb.force_n : fb.position_mm;
  }

  Config config_{};
  ControlMode mode_{ControlMode::kDisplacement};
  PidController force_pid_{};
  PidController disp_pid_{};

  float last_output_{0.0f};
  bool enabled_{true};

  // Состояние рампы захвата после переключения
  bool capture_active_{false};
  float capture_from_{0.0f};
  float capture_elapsed_s_{0.0f};
  float effective_target_{0.0f};

  // Feed-forward: производные эффективной цели между тиками
  float prev_effective_target_{0.0f};
  float prev_target_velocity_{0.0f};
  uint8_t target_history_{0};  ///< 0 — нет истории, 1 — есть v, 2 — есть a

  // Затухающий feed-forward для контура с ki == 0
  float ff_bias_{0.0f};
};

}  // namespace bench
