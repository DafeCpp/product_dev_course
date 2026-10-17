#pragma once

#include <cstddef>
#include <string_view>

#include "bench_command.hpp"
#include "bench_state.hpp"

namespace bench {

/**
 * @brief JSON-представление снимка, ack и команд (контракт для Ф4)
 *
 * Схема: docs/experiment-control/bench-command-schema.md. Всё строится в
 * буфер вызывающего без аллокаций, но ВНЕ hot path 500 Гц (из задачи
 * веб-сервера/линка, паттерн FW-RF8).
 */

/// Размер буфера, достаточный для самого большого снимка
inline constexpr size_t kSnapshotJsonMaxSize = 4096;

/// @return длина JSON или 0, если буфер мал
[[nodiscard]] size_t BuildSnapshotJson(const BenchStateSnapshot& s, char* buf,
                                       size_t cap);

/// @return длина JSON или 0, если буфер мал
[[nodiscard]] size_t BuildAckJson(const CommandAck& ack, char* buf, size_t cap);

enum class ParseError : uint8_t {
  kOk = 0,
  kSyntax,      ///< Не JSON-объект нужной формы
  kUnknownKey,  ///< Неизвестное поле (формат строгий)
  kUnknownType,  ///< Неизвестное значение type/mode
  kMissingField,  ///< Нет cmd_id, type или аргумента команды
  kBadValue,  ///< Значение не того типа/диапазона
};

/**
 * @brief Разобрать команду из JSON плоского объекта
 *
 * Примеры:
 * {"cmd_id":7,"type":"set_setpoint","value":12.5}
 * {"cmd_id":8,"type":"enable","enable":true}
 * {"cmd_id":9,"type":"set_mode","mode":"force"}
 * {"cmd_id":10,"type":"controlled_stop"}
 */
[[nodiscard]] ParseError ParseCommandJson(std::string_view json,
                                          BenchCommand& out);

[[nodiscard]] const char* ToString(CommandType t) noexcept;
[[nodiscard]] const char* ToString(CommandResult r) noexcept;
[[nodiscard]] const char* ToString(RejectReason r) noexcept;
[[nodiscard]] const char* ToString(ControlMode m) noexcept;
[[nodiscard]] const char* ToString(LinkState s) noexcept;
[[nodiscard]] const char* ToString(StopReason r) noexcept;
[[nodiscard]] const char* ToString(EstopChainState s) noexcept;
[[nodiscard]] const char* ToString(JournalKind k) noexcept;
[[nodiscard]] const char* ToString(ParseError e) noexcept;

}  // namespace bench
