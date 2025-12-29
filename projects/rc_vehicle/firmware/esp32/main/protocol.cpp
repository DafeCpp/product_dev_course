#include "protocol.hpp"

#include <string.h>

#include "config.hpp"
#include "esp_log.h"

static const char* TAG = "protocol";
static uint16_t command_seq = 0;

size_t ProtocolBuildCommand(uint8_t* buffer, size_t buffer_size, float throttle,
                            float steering) {
  if (buffer == NULL || buffer_size < 16) {
    return 0;
  }

  // Префикс
  buffer[0] = UART_FRAME_PREFIX_0;
  buffer[1] = UART_FRAME_PREFIX_1;

  // Версия протокола
  buffer[2] = UART_PROTOCOL_VERSION;

  // Тип сообщения: COMMAND
  buffer[3] = UART_MSG_TYPE_COMMAND;

  // Длина payload: SEQ(2) + THR(2) + STEER(2) + FLAGS(1) = 7 байт
  uint16_t payload_len = 7;
  buffer[4] = payload_len & 0xFF;
  buffer[5] = (payload_len >> 8) & 0xFF;

  // Payload
  uint8_t* payload = &buffer[6];

  // SEQ (uint16 LE)
  payload[0] = command_seq & 0xFF;
  payload[1] = (command_seq >> 8) & 0xFF;
  command_seq++;

  // THR_I16 (int16 LE) - нормализованный throttle * 32767
  int16_t thr_i16 = (int16_t)(throttle * 32767.0f);
  payload[2] = thr_i16 & 0xFF;
  payload[3] = (thr_i16 >> 8) & 0xFF;

  // STEER_I16 (int16 LE) - нормализованный steering * 32767
  int16_t steer_i16 = (int16_t)(steering * 32767.0f);
  payload[4] = steer_i16 & 0xFF;
  payload[5] = (steer_i16 >> 8) & 0xFF;

  // FLAGS (bit0: slew_enable)
  payload[6] = 0x00;  // Пока отключено

  // Вычисление CRC16 (по VER..PAYLOAD, без префикса)
  uint16_t crc = ProtocolCrc16(&buffer[2], 4 + payload_len);
  size_t crc_pos = 6 + payload_len;
  buffer[crc_pos] = crc & 0xFF;
  buffer[crc_pos + 1] = (crc >> 8) & 0xFF;

  return crc_pos + 2;  // Общая длина кадра
}

size_t ProtocolParseTelem(const uint8_t* buffer, size_t buffer_size,
                          void* telem_data) {
  // TODO: реализовать парсинг кадров TELEM
  // Проверка префикса, версии, типа, длины, CRC
  // Извлечение данных IMU и статуса
  return 0;
}

uint16_t ProtocolCrc16(const uint8_t* data, size_t length) {
  // CRC-16/IBM (Modbus) полином: 0x8005, начальное значение: 0xFFFF
  uint16_t crc = 0xFFFF;

  for (size_t i = 0; i < length; i++) {
    crc ^= (uint16_t)data[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ 0xA001;  // 0x8005 reversed
      } else {
        crc >>= 1;
      }
    }
  }

  return crc;
}
