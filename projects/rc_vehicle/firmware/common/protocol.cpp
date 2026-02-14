#include "protocol.hpp"

#include <cstring>

size_t ProtocolBuildTelem(std::span<uint8_t> buffer,
                          const TelemetryData &telem_data) {
  constexpr size_t kMinTelemFrameSize = 6 + 15 + 2;  // header + payload + CRC
  if (buffer.size() < kMinTelemFrameSize) {
    return 0;
  }
  buffer[0] = UART_FRAME_PREFIX_0;
  buffer[1] = UART_FRAME_PREFIX_1;
  buffer[2] = UART_PROTOCOL_VERSION;
  buffer[3] = UART_MSG_TYPE_TELEM;
  uint16_t payload_len = 15;
  buffer[4] = payload_len & 0xFF;
  buffer[5] = (payload_len >> 8) & 0xFF;
  buffer[6] = telem_data.seq & 0xFF;
  buffer[7] = (telem_data.seq >> 8) & 0xFF;
  buffer[8] = telem_data.status;
  buffer[9] = telem_data.ax & 0xFF;
  buffer[10] = (telem_data.ax >> 8) & 0xFF;
  buffer[11] = telem_data.ay & 0xFF;
  buffer[12] = (telem_data.ay >> 8) & 0xFF;
  buffer[13] = telem_data.az & 0xFF;
  buffer[14] = (telem_data.az >> 8) & 0xFF;
  buffer[15] = telem_data.gx & 0xFF;
  buffer[16] = (telem_data.gx >> 8) & 0xFF;
  buffer[17] = telem_data.gy & 0xFF;
  buffer[18] = (telem_data.gy >> 8) & 0xFF;
  buffer[19] = telem_data.gz & 0xFF;
  buffer[20] = (telem_data.gz >> 8) & 0xFF;
  uint16_t crc =
      ProtocolCrc16(buffer.subspan(2, 4 + payload_len));
  size_t crc_pos = 6 + payload_len;
  buffer[crc_pos] = crc & 0xFF;
  buffer[crc_pos + 1] = (crc >> 8) & 0xFF;
  return crc_pos + 2;
}

size_t ProtocolParseCommand(std::span<const uint8_t> buffer, float &throttle,
                            float &steering) {
  if (buffer.size() < 16) {
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
  if (payload_len != 7 || buffer.size() < 6 + payload_len + 2) {
    return 0;
  }
  uint16_t recv_crc =
      buffer[6 + payload_len] | (buffer[6 + payload_len + 1] << 8);
  if (recv_crc != ProtocolCrc16(buffer.subspan(2, 4 + payload_len))) {
    return 0;
  }
  int16_t thr_i16 = static_cast<int16_t>(buffer[8] | (buffer[9] << 8));
  int16_t steer_i16 = static_cast<int16_t>(buffer[10] | (buffer[11] << 8));
  throttle = static_cast<float>(thr_i16) / 32767.0f;
  steering = static_cast<float>(steer_i16) / 32767.0f;
  // Защита от -32768 (получается чуть меньше -1.0) и любых выбросов.
  if (throttle > 1.0f) throttle = 1.0f;
  if (throttle < -1.0f) throttle = -1.0f;
  if (steering > 1.0f) steering = 1.0f;
  if (steering < -1.0f) steering = -1.0f;
  return 6 + payload_len + 2;
}

static uint16_t s_command_seq = 0;

size_t ProtocolBuildCommand(std::span<uint8_t> buffer, float throttle,
                            float steering) {
  if (buffer.size() < 16) {
    return 0;
  }
  buffer[0] = UART_FRAME_PREFIX_0;
  buffer[1] = UART_FRAME_PREFIX_1;
  buffer[2] = UART_PROTOCOL_VERSION;
  buffer[3] = UART_MSG_TYPE_COMMAND;
  uint16_t payload_len = 7;
  buffer[4] = payload_len & 0xFF;
  buffer[5] = (payload_len >> 8) & 0xFF;
  buffer[6] = s_command_seq & 0xFF;
  buffer[7] = (s_command_seq >> 8) & 0xFF;
  s_command_seq++;
  int16_t thr_i16 = static_cast<int16_t>(throttle * 32767.0f);
  int16_t steer_i16 = static_cast<int16_t>(steering * 32767.0f);
  buffer[8] = thr_i16 & 0xFF;
  buffer[9] = (thr_i16 >> 8) & 0xFF;
  buffer[10] = steer_i16 & 0xFF;
  buffer[11] = (steer_i16 >> 8) & 0xFF;
  buffer[12] = 0;
  uint16_t crc = ProtocolCrc16(buffer.subspan(2, 4 + payload_len));
  size_t crc_pos = 6 + payload_len;
  buffer[crc_pos] = crc & 0xFF;
  buffer[crc_pos + 1] = (crc >> 8) & 0xFF;
  return crc_pos + 2;
}

size_t ProtocolParseTelem(std::span<const uint8_t> buffer,
                          TelemetryData &telem_data) {
  constexpr size_t kMinTelemFrameSize = 6 + 15 + 2;  // header + payload + CRC
  if (buffer.size() < kMinTelemFrameSize) {
    return 0;
  }
  if (buffer[0] != UART_FRAME_PREFIX_0 || buffer[1] != UART_FRAME_PREFIX_1) {
    return 0;
  }
  if (buffer[2] != UART_PROTOCOL_VERSION || buffer[3] != UART_MSG_TYPE_TELEM) {
    return 0;
  }
  uint16_t payload_len = buffer[4] | (buffer[5] << 8);
  if (payload_len != 15 || buffer.size() < 6 + payload_len + 2) {
    return 0;
  }
  uint16_t recv_crc =
      buffer[6 + payload_len] | (buffer[6 + payload_len + 1] << 8);
  if (recv_crc != ProtocolCrc16(buffer.subspan(2, 4 + payload_len))) {
    return 0;
  }
  telem_data.seq = buffer[6] | (buffer[7] << 8);
  telem_data.status = buffer[8];
  telem_data.ax = static_cast<int16_t>(buffer[9] | (buffer[10] << 8));
  telem_data.ay = static_cast<int16_t>(buffer[11] | (buffer[12] << 8));
  telem_data.az = static_cast<int16_t>(buffer[13] | (buffer[14] << 8));
  telem_data.gx = static_cast<int16_t>(buffer[15] | (buffer[16] << 8));
  telem_data.gy = static_cast<int16_t>(buffer[17] | (buffer[18] << 8));
  telem_data.gz = static_cast<int16_t>(buffer[19] | (buffer[20] << 8));
  return 6 + payload_len + 2;
}

size_t ProtocolBuildLog(std::span<uint8_t> buffer, const char *msg,
                        size_t msg_len) {
  if (msg_len > PROTOCOL_LOG_MAX_PAYLOAD) msg_len = PROTOCOL_LOG_MAX_PAYLOAD;
  size_t frame_size = 6 + msg_len + 2;
  if (buffer.size() < frame_size) return 0;
  buffer[0] = UART_FRAME_PREFIX_0;
  buffer[1] = UART_FRAME_PREFIX_1;
  buffer[2] = UART_PROTOCOL_VERSION;
  buffer[3] = UART_MSG_TYPE_LOG;
  buffer[4] = static_cast<uint8_t>(msg_len & 0xFF);
  buffer[5] = static_cast<uint8_t>((msg_len >> 8) & 0xFF);
  memcpy(buffer.data() + 6, msg, msg_len);
  uint16_t crc = ProtocolCrc16(buffer.subspan(2, 4 + msg_len));
  buffer[6 + msg_len] = crc & 0xFF;
  buffer[6 + msg_len + 1] = (crc >> 8) & 0xFF;
  return frame_size;
}

size_t ProtocolParseLog(std::span<const uint8_t> buffer, char *msg,
                        size_t max_msg_len, size_t &msg_len) {
  if (buffer.size() < 8) return 0;
  if (buffer[0] != UART_FRAME_PREFIX_0 || buffer[1] != UART_FRAME_PREFIX_1)
    return 0;
  if (buffer[2] != UART_PROTOCOL_VERSION || buffer[3] != UART_MSG_TYPE_LOG)
    return 0;
  uint16_t payload_len = buffer[4] | (buffer[5] << 8);
  if (payload_len > PROTOCOL_LOG_MAX_PAYLOAD) return 0;
  size_t frame_size = 6 + payload_len + 2;
  if (buffer.size() < frame_size) return 0;
  uint16_t recv_crc =
      buffer[6 + payload_len] | (buffer[6 + payload_len + 1] << 8);
  if (recv_crc != ProtocolCrc16(buffer.subspan(2, 4 + payload_len))) return 0;
  msg_len = payload_len < max_msg_len ? payload_len : max_msg_len;
  memcpy(msg, buffer.data() + 6, msg_len);
  return frame_size;
}

uint16_t ProtocolCrc16(std::span<const uint8_t> data) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < data.size(); i++) {
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

int ProtocolFindFrameStart(std::span<const uint8_t> buffer) {
  if (buffer.size() < 2) {
    return -1;
  }
  for (size_t i = 0; i < buffer.size() - 1; i++) {
    if (buffer[i] == UART_FRAME_PREFIX_0 &&
        buffer[i + 1] == UART_FRAME_PREFIX_1) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

static constexpr size_t kPingPongFrameSize = 6 + 0 + 2;  // header + 0 payload + CRC

size_t ProtocolBuildPing(std::span<uint8_t> buffer) {
  if (buffer.size() < kPingPongFrameSize) return 0;
  buffer[0] = UART_FRAME_PREFIX_0;
  buffer[1] = UART_FRAME_PREFIX_1;
  buffer[2] = UART_PROTOCOL_VERSION;
  buffer[3] = UART_MSG_TYPE_PING;
  buffer[4] = 0;
  buffer[5] = 0;
  uint16_t crc = ProtocolCrc16(buffer.subspan(2, 4));
  buffer[6] = crc & 0xFF;
  buffer[7] = (crc >> 8) & 0xFF;
  return kPingPongFrameSize;
}

size_t ProtocolParsePing(std::span<const uint8_t> buffer) {
  if (buffer.size() < kPingPongFrameSize) return 0;
  if (buffer[0] != UART_FRAME_PREFIX_0 || buffer[1] != UART_FRAME_PREFIX_1)
    return 0;
  if (buffer[2] != UART_PROTOCOL_VERSION || buffer[3] != UART_MSG_TYPE_PING)
    return 0;
  if (buffer[4] != 0 || buffer[5] != 0) return 0;
  uint16_t recv_crc = buffer[6] | (buffer[7] << 8);
  if (recv_crc != ProtocolCrc16(buffer.subspan(2, 4))) return 0;
  return kPingPongFrameSize;
}

size_t ProtocolBuildPong(std::span<uint8_t> buffer) {
  if (buffer.size() < kPingPongFrameSize) return 0;
  buffer[0] = UART_FRAME_PREFIX_0;
  buffer[1] = UART_FRAME_PREFIX_1;
  buffer[2] = UART_PROTOCOL_VERSION;
  buffer[3] = UART_MSG_TYPE_PONG;
  buffer[4] = 0;
  buffer[5] = 0;
  uint16_t crc = ProtocolCrc16(buffer.subspan(2, 4));
  buffer[6] = crc & 0xFF;
  buffer[7] = (crc >> 8) & 0xFF;
  return kPingPongFrameSize;
}

size_t ProtocolParsePong(std::span<const uint8_t> buffer) {
  if (buffer.size() < kPingPongFrameSize) return 0;
  if (buffer[0] != UART_FRAME_PREFIX_0 || buffer[1] != UART_FRAME_PREFIX_1)
    return 0;
  if (buffer[2] != UART_PROTOCOL_VERSION || buffer[3] != UART_MSG_TYPE_PONG)
    return 0;
  if (buffer[4] != 0 || buffer[5] != 0) return 0;
  uint16_t recv_crc = buffer[6] | (buffer[7] << 8);
  if (recv_crc != ProtocolCrc16(buffer.subspan(2, 4))) return 0;
  return kPingPongFrameSize;
}
