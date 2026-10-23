#pragma once

#include <cstdint>

namespace rc_vehicle {

/**
 * @brief Телеметрия ESC (LOS-19)
 *
 * Не зависит от протокола: сейчас заполняется из ответа VESC COMM_GET_VALUES
 * по UART, но тот же снимок подойдёт и для другого ESC с телеметрией.
 * POD — копируется в TelemetrySnapshot.
 */
struct EscTelemetry {
  float erpm{0.0f};             ///< Электрические обороты [об/мин]
  float input_voltage_v{0.0f};  ///< Напряжение батареи на входе ESC [В]
  float motor_current_a{0.0f};  ///< Средний ток мотора [А]
  float input_current_a{0.0f};  ///< Средний ток батареи [А]
  float duty{0.0f};             ///< Скважность [-1..1]
  float temp_fet_c{0.0f};       ///< Температура силовых ключей [°C]
  float temp_motor_c{0.0f};     ///< Температура мотора [°C] (без датчика ~0)
  int32_t tachometer{0};        ///< Счётчик коммутаций (6 на эл. оборот)
  uint8_t fault_code{0};        ///< Код ошибки ESC (0 — нет ошибки)
  uint32_t age_ms{0};           ///< Возраст снимка на момент чтения [мс]
};

}  // namespace rc_vehicle
