#pragma once

/**
 * Базовый класс компонента прошивки (PWM, RC, IMU, UART-мост, failsafe и т.д.).
 * Единый интерфейс инициализации; при необходимости контекст передаётся в конструктор
 * конкретного компонента (Context&).
 */
class BaseComponent {
 public:
  virtual ~BaseComponent() = default;

  /** Инициализация компонента. 0 — успех, -1 — ошибка. */
  virtual int Init() = 0;

  /** Имя для логов (по умолчанию "component"). */
  virtual const char *Name() const { return "component"; }
};
