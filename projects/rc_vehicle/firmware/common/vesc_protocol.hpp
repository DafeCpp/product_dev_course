#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "esc_telemetry.hpp"

/**
 * Протокол VESC по UART (LOS-19): кадрирование пакетов, CRC и разбор ответа
 * COMM_GET_VALUES. Платформенно-независимый, без аллокаций.
 *
 * Кадр (как в bldc/packet.c):
 *   короткий: 0x02, len (1 байт),   payload, crc16 (BE), 0x03
 *   длинный:  0x03, len (2 байта BE), payload, crc16 (BE), 0x03
 * CRC — CRC-16/XMODEM (poly 0x1021, init 0) по payload.
 * Числа в payload — big-endian; «float16/float32» VESC — это int16/int32,
 * умноженные на масштаб.
 */
namespace rc_vehicle::vesc {

/// Команда «прочитать значения» (COMM_PACKET_ID::COMM_GET_VALUES).
inline constexpr uint8_t kCommGetValues = 4;

/// Достаточно для любых ответов, которые мы разбираем (GET_VALUES ~74 байта).
inline constexpr size_t kMaxPayload = 256;

/// Размер кадра с payload длиной len (короткий формат).
inline constexpr size_t FrameSize(size_t len) { return len + 5; }

[[nodiscard]] uint16_t Crc16(std::span<const uint8_t> data) noexcept;

/**
 * Упаковать payload в кадр.
 * @return Длина кадра или 0, если payload пуст / длиннее kMaxPayload или не
 *         помещается в out.
 */
[[nodiscard]] size_t EncodePacket(std::span<const uint8_t> payload,
                                  std::span<uint8_t> out) noexcept;

/** Кадр запроса COMM_GET_VALUES. */
[[nodiscard]] size_t BuildGetValuesRequest(std::span<uint8_t> out) noexcept;

/**
 * Потоковый разбор кадров: байты подаются по одному, мусор между кадрами и
 * кадры с плохим CRC отбрасываются.
 */
class PacketParser {
 public:
  /**
   * @return payload принятого кадра; view действителен до следующего Feed/Reset
   */
  [[nodiscard]] std::optional<std::span<const uint8_t>> Feed(
      uint8_t byte) noexcept;

  void Reset() noexcept { state_ = State::Start; }

  /// Сколько кадров отброшено по CRC/терминатору/длине (диагностика).
  [[nodiscard]] uint32_t ErrorCount() const noexcept { return errors_; }

 private:
  enum class State : uint8_t {
    Start,
    LenHigh,
    LenLow,
    Payload,
    CrcHigh,
    CrcLow,
    End
  };

  void Fail() noexcept;

  State state_{State::Start};
  size_t len_{0};
  size_t pos_{0};
  uint16_t crc_{0};
  uint32_t errors_{0};
  std::array<uint8_t, kMaxPayload> buf_{};
};

/**
 * Разобрать payload ответа COMM_GET_VALUES (первый байт — id команды).
 * Поля после fault_code (есть в новых прошивках VESC) игнорируются.
 * @return nullopt, если это не GET_VALUES или payload короче обязательной
 *         части. age_ms в результате = 0.
 */
[[nodiscard]] std::optional<EscTelemetry> ParseGetValues(
    std::span<const uint8_t> payload) noexcept;

}  // namespace rc_vehicle::vesc
