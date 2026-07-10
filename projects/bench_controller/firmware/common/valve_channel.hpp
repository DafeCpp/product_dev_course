#pragma once

#include <cstdint>

#include "bench_types.hpp"

namespace bench {

/**
 * @brief Канал управления клапаном — единственный шов между портируемым
 *        ядром и транспортом (CANopen / фейк в тестах)
 *
 * Реализации:
 * - CanopenValveChannel — поверх OD CANopenNode (RPDO setpoint /
 *   TPDO feedback), PR-B;
 * - FakeValveChannel / PlantValveChannel — тестовые (fixtures).
 *
 * PreTick/PostTick — хуки транспорта вокруг шага регулятора: CANopen-
 * реализация в PreTick дренирует шину и обрабатывает SYNC/RPDO, в
 * PostTick отправляет TPDO и ведёт NMT/HB-хозяйство. Дефолт — no-op.
 */
class IValveChannel {
 public:
  virtual ~IValveChannel() = default;

  /// Обработка входящего трафика перед шагом регулятора
  virtual void PreTick(uint32_t dt_us) { (void)dt_us; }

  /// Записать команду клапану (уйдёт в PostTick)
  virtual void WriteSetpoint(const ValveSetpoint& sp) = 0;

  /// Прочитать последнюю обратную связь (без блокировки)
  [[nodiscard]] virtual ValveFeedback ReadFeedback() = 0;

  /// Отправка исходящего трафика после шага регулятора
  virtual void PostTick(uint32_t dt_us) { (void)dt_us; }

  /// Узел клапана в рабочем состоянии (NMT operational, heartbeat свежий)
  [[nodiscard]] virtual bool IsOperational() const = 0;
};

}  // namespace bench
