#pragma once

#include <optional>

#include "esc_telemetry.hpp"

/**
 * Опрос VESC по UART (LOS-19): фоновая задача на core 0 раз в
 * VESC_POLL_INTERVAL_MS шлёт COMM_GET_VALUES и хранит последний ответ.
 * Газ по-прежнему идёт PWM-сигналом — UART только читает телеметрию, поэтому
 * обрыв провода или зависание VESC не влияют на управление и failsafe.
 *
 * Запускается только для профиля мотора vesc_pwm.
 */

/**
 * Настроить UART и запустить задачу опроса. Повторный вызов — no-op.
 * @return 0 при успехе, -1 при ошибке (телеметрии не будет, управление
 *         работает)
 */
int VescUartInit(void);

/**
 * Последняя телеметрия с age_ms. nullopt, если опрос не запущен, ответов ещё
 * не было или последний старше VESC_TELEM_STALE_MS. Безопасно вызывать из
 * control loop: только копия под короткой критической секцией.
 */
std::optional<rc_vehicle::EscTelemetry> VescUartGetTelemetry(void);
