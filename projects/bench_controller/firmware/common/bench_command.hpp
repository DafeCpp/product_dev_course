#pragma once

#include <cstdint>

#include "bench_types.hpp"

namespace bench {

/**
 * @brief Типы команд единого командного тракта
 *
 * Тот же тракт принимает команды из веб-страницы головы, с экрана
 * (LOS-180) и из UART-линка от STM32 (LOS-181): боковых дверей мимо
 * идемпотентности и журнала нет (P-1/P-5 из LOS-74).
 */
enum class CommandType : uint8_t {
  kEnable = 0,  ///< Разрешение/запрет работы канала (args: enable)
  kSetMode,      ///< Режим force/displacement, bumpless (args: mode)
  kSetSetpoint,  ///< Ручная уставка в единицах активного режима (args: value)
  kControlledStop,  ///< P-1: остановка программы и удержание позиции
  kEStopSoft,  ///< P-2: программное снятие enable. НЕ защита людей
};

/**
 * @brief Команда оператора
 *
 * POD без выделения памяти: путь «UI → очередь → контур» не аллоцирует.
 * Поля args используются по типу команды, остальные игнорируются.
 */
struct BenchCommand {
  uint32_t cmd_id{0};  ///< Идентификатор для идемпотентности; 0 зарезервирован
  CommandType type{CommandType::kEnable};
  ControlMode mode{ControlMode::kDisplacement};  ///< kSetMode
  float value{0.0f};                             ///< kSetSetpoint
  bool enable{false};                            ///< kEnable
};

enum class CommandResult : uint8_t { kAccepted = 0, kRejected };

/// Причина отказа (nack). Для принятых команд — kNone.
enum class RejectReason : uint8_t {
  kNone = 0,
  kInvalidId,       ///< cmd_id == 0
  kProgramRunning,  ///< Идёт программа нагружения (S2-ограничение)
  kHeld,  ///< Защитное удержание (разрушение / потеря связи)
  kStopped,  ///< Операторский останов: сначала enable(true)
  kOutOfRange,  ///< Значение вне лимитов или не число
};

/**
 * @brief Подтверждение команды (ack/nack)
 *
 * Повтор команды с тем же cmd_id возвращает исходный результат с
 * duplicate = true и не исполняется повторно.
 */
struct CommandAck {
  uint32_t cmd_id{0};
  CommandType type{CommandType::kEnable};
  CommandResult result{CommandResult::kAccepted};
  RejectReason reason{RejectReason::kNone};
  bool duplicate{false};
};

/**
 * @brief Очередь команд в контур (single-slot overwrite)
 *
 * Паттерн RC Vehicle (SendWifiCommand → xQueueOverwrite): производитель
 * (веб/UART/экран) кладёт последнюю команду, контур в начале тика
 * забирает без блокировки. Если новая команда пришла до того, как
 * контур забрал предыдущую, предыдущая теряется (счётчик
 * OverwrittenCount) — клиент обязан дождаться ack перед следующей.
 *
 * Реализации: LockedCommandSlot (host/тесты, command_slot.hpp);
 * FreeRTOS (xQueueOverwrite / xQueueReceive(..., 0)) — в платформенном
 * слое прошивки головы.
 */
class ICommandInbox {
 public:
  virtual ~ICommandInbox() = default;

  /// Положить команду (вызывается из любой задачи, кроме контура)
  virtual void Post(const BenchCommand& cmd) = 0;

  /// Забрать команду без ожидания (вызывается из контура, hot path)
  [[nodiscard]] virtual bool TryTake(BenchCommand& out) = 0;

  /// Сколько команд потеряно перезаписью
  [[nodiscard]] virtual uint32_t OverwrittenCount() const = 0;
};

}  // namespace bench
