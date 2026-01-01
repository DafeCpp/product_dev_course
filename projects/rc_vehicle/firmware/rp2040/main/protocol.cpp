#include "protocol.hpp"

#include <string.h>

#include "config.hpp"

// Счётчик последовательности управляется из main.cpp

size_t ProtocolBuildTelem(uint8_t *buffer, size_t buffer_size,
                          const TelemetryData *telem_data) {
  if (buffer == NULL || buffer_size < 20 || telem_data == NULL) {
    return 0;
  }

  // Префикс
  buffer[0] = UART_FRAME_PREFIX_0;
  buffer[1] = UART_FRAME_PREFIX_1;

  // Версия протокола
  buffer[2] = UART_PROTOCOL_VERSION;

  // Тип сообщения: TELEM
  buffer[3] = UART_MSG_TYPE_TELEM;

  // Длина payload: SEQ(2) + STATUS(1) + AX(2) + AY(2) + AZ(2) + GX(2) + GY(2) +
  // GZ(2) = 15 байт
  uint16_t payload_len = 15;
  buffer[4] = payload_len & 0xFF;
  buffer[5] = (payload_len >> 8) & 0xFF;

  // Payload
  uint8_t *payload = &buffer[6];

  // SEQ (uint16 LE)
  payload[0] = telem_data->seq & 0xFF;
  payload[1] = (telem_data->seq >> 8) & 0xFF;

  // STATUS
  payload[2] = telem_data->status;

  // AX, AY, AZ (int16 LE)
  payload[3] = telem_data->ax & 0xFF;
  payload[4] = (telem_data->ax >> 8) & 0xFF;
  payload[5] = telem_data->ay & 0xFF;
  payload[6] = (telem_data->ay >> 8) & 0xFF;
  payload[7] = telem_data->az & 0xFF;
  payload[8] = (telem_data->az >> 8) & 0xFF;

  // GX, GY, GZ (int16 LE)
  payload[9] = telem_data->gx & 0xFF;
  payload[10] = (telem_data->gx >> 8) & 0xFF;
  payload[11] = telem_data->gy & 0xFF;
  payload[12] = (telem_data->gy >> 8) & 0xFF;
  payload[13] = telem_data->gz & 0xFF;
  payload[14] = (telem_data->gz >> 8) & 0xFF;

  // Вычисление CRC16 (по VER..PAYLOAD, без префикса)
  uint16_t crc = ProtocolCrc16(&buffer[2], 4 + payload_len);
  size_t crc_pos = 6 + payload_len;
  buffer[crc_pos] = crc & 0xFF;
  buffer[crc_pos + 1] = (crc >> 8) & 0xFF;

  return crc_pos + 2; // Общая длина кадра
}

size_t ProtocolParseCommand(const uint8_t *buffer, size_t buffer_size,
                            float *throttle, float *steering) {
  if (buffer == NULL || buffer_size < 16 || throttle == NULL ||
      steering == NULL) {
    return 0;
  }

  // Проверка префикса
  if (buffer[0] != UART_FRAME_PREFIX_0 || buffer[1] != UART_FRAME_PREFIX_1) {
    return 0;
  }

  // Проверка версии
  if (buffer[2] != UART_PROTOCOL_VERSION) {
    return 0;
  }

  // Проверка типа
  if (buffer[3] != UART_MSG_TYPE_COMMAND) {
    return 0;
  }

  // Чтение длины payload
  uint16_t payload_len = buffer[4] | (buffer[5] << 8);
  if (payload_len != 7 || buffer_size < 6 + payload_len + 2) {
    return 0;
  }

  // Проверка CRC16
  uint16_t received_crc =
      buffer[6 + payload_len] | (buffer[6 + payload_len + 1] << 8);
  uint16_t calculated_crc = ProtocolCrc16(&buffer[2], 4 + payload_len);
  if (received_crc != calculated_crc) {
    return 0;
  }

  // Извлечение данных из payload
  const uint8_t *payload = &buffer[6];

  // SEQ (пока не используем)
  // uint16_t seq = payload[0] | (payload[1] << 8);

  // THR_I16 (int16 LE)
  int16_t thr_i16 = payload[2] | (payload[3] << 8);
  *throttle = (float)thr_i16 / 32767.0f;

  // STEER_I16 (int16 LE)
  int16_t steer_i16 = payload[4] | (payload[5] << 8);
  *steering = (float)steer_i16 / 32767.0f;

  // FLAGS (пока не используем)
  // uint8_t flags = payload[6];

  return 6 + payload_len + 2; // Общая длина кадра
}

uint16_t ProtocolCrc16(const uint8_t *data, size_t length) {
  // CRC-16/IBM (Modbus) полином: 0x8005, начальное значение: 0xFFFF
  uint16_t crc = 0xFFFF;

  for (size_t i = 0; i < length; i++) {
    crc ^= (uint16_t)data[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ 0xA001; // 0x8005 reversed
      } else {
        crc >>= 1;
      }
    }
  }

  return crc;
}

int ProtocolFindFrameStart(const uint8_t *buffer, size_t buffer_size) {
  if (buffer == NULL || buffer_size < 2) {
    return -1;
  }

  for (size_t i = 0; i < buffer_size - 1; i++) {
    if (buffer[i] == UART_FRAME_PREFIX_0 &&
        buffer[i + 1] == UART_FRAME_PREFIX_1) {
      return (int)i;
    }
  }

  return -1;
}
