#pragma once

/**
 * LPF Butterworth 2-го порядка (один канал).
 *
 * Используется для фильтрации угловой скорости по оси Z (gyro Z) перед
 * подачей в ПИД контроля рыскания (yaw rate). Убирает высокочастотный шум
 * и вибрации при сохранении отклика на реальные повороты.
 *
 * Параметры: частота среза fc (Hz), частота дискретизации fs (Hz).
 * Реализация: билинейное преобразование (Bilinear / Tustin).
 * Платформонезависимый код (только float, без зависимостей от RTOS).
 */
class LpfButterworth2 {
 public:
  LpfButterworth2();

  /**
   * Настроить фильтр.
   * @param cutoff_hz Частота среза (Hz), например 20–50 для yaw rate
   * @param sample_rate_hz Частота дискретизации (Hz), например 500
   */
  void SetParams(float cutoff_hz, float sample_rate_hz);

  /**
   * Подать очередной семпл и получить отфильтрованное значение.
   * @param x Входное значение (например, gyro Z в dps)
   * @return Отфильтрованное значение
   */
  float Step(float x);

  /**
   * Текущее отфильтрованное значение (последний результат Step()).
   */
  float GetOutput() const noexcept { return y1_; }

  /** Сброс состояния фильтра (история входа/выхода). */
  void Reset();

  /** Частота среза (Hz). */
  float GetCutoffHz() const noexcept { return cutoff_hz_; }

  /** Частота дискретизации (Hz). */
  float GetSampleRateHz() const noexcept { return sample_rate_hz_; }

  /** Признак валидной настройки (SetParams вызван с положительными частотами). */
  bool IsConfigured() const noexcept { return configured_; }

 private:
  float cutoff_hz_{0.f};
  float sample_rate_hz_{0.f};
  bool configured_{false};

  // Коэффициенты: y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2 (a0 = 1)
  float b0_{1.f}, b1_{0.f}, b2_{0.f};
  float a1_{0.f}, a2_{0.f};

  // Состояние: x[n-1], x[n-2], y[n-1], y[n-2]
  float x1_{0.f}, x2_{0.f};
  float y1_{0.f}, y2_{0.f};

  void UpdateCoefficients();
};
