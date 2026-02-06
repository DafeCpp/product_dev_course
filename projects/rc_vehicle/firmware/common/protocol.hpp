#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

// Общие константы протокола UART (RP2040/STM32 ↔ ESP32)
#define UART_FRAME_PREFIX_0 0xAA
#define UART_FRAME_PREFIX_1 0x55
#define UART_PROTOCOL_VERSION 0x01

#define UART_MSG_TYPE_COMMAND 0x01
#define UART_MSG_TYPE_TELEM 0x02
#define UART_MSG_TYPE_PING 0x03
#define UART_MSG_TYPE_PONG 0x04

// Структура данных телеметрии (тот же формат для RP2040 и STM32)
struct TelemetryData {
  uint16_t seq;
  uint8_t status;     // bit0: rc_ok, bit1: wifi_ok, bit2: failsafe_active
  int16_t ax, ay, az; // Акселерометр (mg)
  int16_t gx, gy, gz; // Гироскоп (mdps)
};

size_t ProtocolBuildTelem(std::span<uint8_t> buffer,
                          const TelemetryData &telem_data);
size_t ProtocolParseCommand(std::span<const uint8_t> buffer, float &throttle,
                            float &steering);

/** COMMAND (ESP32 → MCU): throttle/steering. Для ESP32. */
size_t ProtocolBuildCommand(std::span<uint8_t> buffer, float throttle,
                            float steering);
/** TELEM (MCU → ESP32): парсинг в TelemetryData. Для ESP32. */
size_t ProtocolParseTelem(std::span<const uint8_t> buffer,
                          TelemetryData &telem_data);

uint16_t ProtocolCrc16(std::span<const uint8_t> data);
int ProtocolFindFrameStart(std::span<const uint8_t> buffer);

/** PING (ESP32 → MCU): проверка связи, без payload. */
size_t ProtocolBuildPing(std::span<uint8_t> buffer);
/** Распарсить входящий PING. Возврат: длина кадра (8) или 0. */
size_t ProtocolParsePing(std::span<const uint8_t> buffer);
/** PONG (MCU → ESP32): ответ на PING. */
size_t ProtocolBuildPong(std::span<uint8_t> buffer);
/** Распарсить входящий PONG. Возврат: длина кадра (8) или 0. */
size_t ProtocolParsePong(std::span<const uint8_t> buffer);
