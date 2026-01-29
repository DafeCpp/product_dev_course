#include "protocol.hpp"

size_t ProtocolBuildTelem(uint8_t *buffer, size_t buffer_size,
                          const TelemetryData *telem_data) {
  if (buffer == nullptr || buffer_size < 20 || telem_data == nullptr) {
    return 0;
  }
  buffer[0] = UART_FRAME_PREFIX_0;
  buffer[1] = UART_FRAME_PREFIX_1;
  buffer[2] = UART_PROTOCOL_VERSION;
  buffer[3] = UART_MSG_TYPE_TELEM;
  uint16_t payload_len = 15;
  buffer[4] = payload_len & 0xFF;
  buffer[5] = (payload_len >> 8) & 0xFF;
  uint8_t *p = &buffer[6];
  p[0] = telem_data->seq & 0xFF;
  p[1] = (telem_data->seq >> 8) & 0xFF;
  p[2] = telem_data->status;
  p[3] = telem_data->ax & 0xFF;
  p[4] = (telem_data->ax >> 8) & 0xFF;
  p[5] = telem_data->ay & 0xFF;
  p[6] = (telem_data->ay >> 8) & 0xFF;
  p[7] = telem_data->az & 0xFF;
  p[8] = (telem_data->az >> 8) & 0xFF;
  p[9] = telem_data->gx & 0xFF;
  p[10] = (telem_data->gx >> 8) & 0xFF;
  p[11] = telem_data->gy & 0xFF;
  p[12] = (telem_data->gy >> 8) & 0xFF;
  p[13] = telem_data->gz & 0xFF;
  p[14] = (telem_data->gz >> 8) & 0xFF;
  uint16_t crc = ProtocolCrc16(&buffer[2], 4 + payload_len);
  size_t crc_pos = 6 + payload_len;
  buffer[crc_pos] = crc & 0xFF;
  buffer[crc_pos + 1] = (crc >> 8) & 0xFF;
  return crc_pos + 2;
}

size_t ProtocolParseCommand(const uint8_t *buffer, size_t buffer_size,
                            float *throttle, float *steering) {
  if (buffer == nullptr || buffer_size < 16 || throttle == nullptr ||
      steering == nullptr) {
    return 0;
  }
  if (buffer[0] != UART_FRAME_PREFIX_0 || buffer[1] != UART_FRAME_PREFIX_1) {
    return 0;
  }
  if (buffer[2] != UART_PROTOCOL_VERSION ||
      buffer[3] != UART_MSG_TYPE_COMMAND) {
    return 0;
  }
  uint16_t payload_len = buffer[4] | (buffer[5] << 8);
  if (payload_len != 7 || buffer_size < 6 + payload_len + 2) {
    return 0;
  }
  uint16_t recv_crc =
      buffer[6 + payload_len] | (buffer[6 + payload_len + 1] << 8);
  if (recv_crc != ProtocolCrc16(&buffer[2], 4 + payload_len)) {
    return 0;
  }
  const uint8_t *p = &buffer[6];
  int16_t thr_i16 = static_cast<int16_t>(p[2] | (p[3] << 8));
  int16_t steer_i16 = static_cast<int16_t>(p[4] | (p[5] << 8));
  *throttle = static_cast<float>(thr_i16) / 32767.0f;
  *steering = static_cast<float>(steer_i16) / 32767.0f;
  return 6 + payload_len + 2;
}

static uint16_t s_command_seq = 0;

size_t ProtocolBuildCommand(uint8_t *buffer, size_t buffer_size,
                            float throttle, float steering) {
  if (buffer == nullptr || buffer_size < 16) {
    return 0;
  }
  buffer[0] = UART_FRAME_PREFIX_0;
  buffer[1] = UART_FRAME_PREFIX_1;
  buffer[2] = UART_PROTOCOL_VERSION;
  buffer[3] = UART_MSG_TYPE_COMMAND;
  uint16_t payload_len = 7;
  buffer[4] = payload_len & 0xFF;
  buffer[5] = (payload_len >> 8) & 0xFF;
  uint8_t *p = &buffer[6];
  p[0] = s_command_seq & 0xFF;
  p[1] = (s_command_seq >> 8) & 0xFF;
  s_command_seq++;
  int16_t thr_i16 = static_cast<int16_t>(throttle * 32767.0f);
  int16_t steer_i16 = static_cast<int16_t>(steering * 32767.0f);
  p[2] = thr_i16 & 0xFF;
  p[3] = (thr_i16 >> 8) & 0xFF;
  p[4] = steer_i16 & 0xFF;
  p[5] = (steer_i16 >> 8) & 0xFF;
  p[6] = 0;
  uint16_t crc = ProtocolCrc16(&buffer[2], 4 + payload_len);
  size_t crc_pos = 6 + payload_len;
  buffer[crc_pos] = crc & 0xFF;
  buffer[crc_pos + 1] = (crc >> 8) & 0xFF;
  return crc_pos + 2;
}

size_t ProtocolParseTelem(const uint8_t *buffer, size_t buffer_size,
                          TelemetryData *telem_data) {
  if (buffer == nullptr || buffer_size < 20 || telem_data == nullptr) {
    return 0;
  }
  if (buffer[0] != UART_FRAME_PREFIX_0 || buffer[1] != UART_FRAME_PREFIX_1) {
    return 0;
  }
  if (buffer[2] != UART_PROTOCOL_VERSION || buffer[3] != UART_MSG_TYPE_TELEM) {
    return 0;
  }
  uint16_t payload_len = buffer[4] | (buffer[5] << 8);
  if (payload_len != 15 || buffer_size < 6 + payload_len + 2) {
    return 0;
  }
  uint16_t recv_crc =
      buffer[6 + payload_len] | (buffer[6 + payload_len + 1] << 8);
  if (recv_crc != ProtocolCrc16(&buffer[2], 4 + payload_len)) {
    return 0;
  }
  const uint8_t *p = &buffer[6];
  telem_data->seq = p[0] | (p[1] << 8);
  telem_data->status = p[2];
  telem_data->ax = static_cast<int16_t>(p[3] | (p[4] << 8));
  telem_data->ay = static_cast<int16_t>(p[5] | (p[6] << 8));
  telem_data->az = static_cast<int16_t>(p[7] | (p[8] << 8));
  telem_data->gx = static_cast<int16_t>(p[9] | (p[10] << 8));
  telem_data->gy = static_cast<int16_t>(p[11] | (p[12] << 8));
  telem_data->gz = static_cast<int16_t>(p[13] | (p[14] << 8));
  return 6 + payload_len + 2;
}

uint16_t ProtocolCrc16(const uint8_t *data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < length; i++) {
    crc ^= static_cast<uint16_t>(data[i]);
    for (int j = 0; j < 8; j++) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

int ProtocolFindFrameStart(const uint8_t *buffer, size_t buffer_size) {
  if (buffer == nullptr || buffer_size < 2) {
    return -1;
  }
  for (size_t i = 0; i < buffer_size - 1; i++) {
    if (buffer[i] == UART_FRAME_PREFIX_0 &&
        buffer[i + 1] == UART_FRAME_PREFIX_1) {
      return static_cast<int>(i);
    }
  }
  return -1;
}
