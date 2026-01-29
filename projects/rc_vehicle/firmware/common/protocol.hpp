#pragma once

#include <stddef.h>
#include <stdint.h>

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

size_t ProtocolBuildTelem(uint8_t *buffer, size_t buffer_size,
                          const TelemetryData *telem_data);
size_t ProtocolParseCommand(const uint8_t *buffer, size_t buffer_size,
                            float *throttle, float *steering);

/** COMMAND (ESP32 → MCU): throttle/steering. Для ESP32. */
size_t ProtocolBuildCommand(uint8_t *buffer, size_t buffer_size,
                            float throttle, float steering);
/** TELEM (MCU → ESP32): парсинг в TelemetryData. Для ESP32. */
size_t ProtocolParseTelem(const uint8_t *buffer, size_t buffer_size,
                          TelemetryData *telem_data);

uint16_t ProtocolCrc16(const uint8_t *data, size_t length);
int ProtocolFindFrameStart(const uint8_t *buffer, size_t buffer_size);
