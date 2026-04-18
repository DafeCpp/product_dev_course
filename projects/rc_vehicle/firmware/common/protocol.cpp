#include "protocol.hpp"

#include <cstring>

namespace rc_vehicle::protocol {

// ═══════════════════════════════════════════════════════════════════════════
// Статические переменные
// ═══════════════════════════════════════════════════════════════════════════

uint16_t Protocol::next_command_seq_ = 0;

// ═══════════════════════════════════════════════════════════════════════════
// FrameBuilder - построение кадров
// ═══════════════════════════════════════════════════════════════════════════

void FrameBuilder::WriteHeader(std::span<uint8_t> buffer,
                               uint16_t payload_len) const noexcept {
  buffer[0] = FRAME_PREFIX_0;
  buffer[1] = FRAME_PREFIX_1;
  buffer[2] = PROTOCOL_VERSION;
  buffer[3] = static_cast<uint8_t>(type_);
  buffer[4] = payload_len & 0xFF;
  buffer[5] = (payload_len >> 8) & 0xFF;
}

void FrameBuilder::WriteCrc(std::span<uint8_t> buffer,
                            size_t payload_len) const noexcept {
  // CRC вычисляется от версии до конца payload (исключая prefix)
  uint16_t crc = Protocol::CalculateCrc16(buffer.subspan(2, 4 + payload_len));
  size_t crc_pos = HEADER_SIZE + payload_len;
  buffer[crc_pos] = crc & 0xFF;
  buffer[crc_pos + 1] = (crc >> 8) & 0xFF;
}

Result<size_t> FrameBuilder::Build(
    std::span<uint8_t> buffer,
    std::span<const uint8_t> payload) const noexcept {
  const size_t frame_size = HEADER_SIZE + payload.size() + CRC_SIZE;

  if (buffer.size() < frame_size) {
    return ParseError::BufferTooSmall;
  }

  WriteHeader(buffer, static_cast<uint16_t>(payload.size()));

  // Копируем payload
  if (!payload.empty()) {
    std::memcpy(buffer.data() + HEADER_SIZE, payload.data(), payload.size());
  }

  WriteCrc(buffer, payload.size());

  return frame_size;
}

// ═══════════════════════════════════════════════════════════════════════════
// FrameParser - парсинг кадров
// ═══════════════════════════════════════════════════════════════════════════

Result<MessageType> FrameParser::ValidateHeader(
    std::span<const uint8_t> buffer) noexcept {
  if (buffer.size() < 4) {
    return ParseError::InsufficientData;
  }

  if (buffer[0] != FRAME_PREFIX_0 || buffer[1] != FRAME_PREFIX_1) {
    return ParseError::InvalidPrefix;
  }

  if (buffer[2] != PROTOCOL_VERSION) {
    return ParseError::InvalidVersion;
  }

  return static_cast<MessageType>(buffer[3]);
}

Result<uint16_t> FrameParser::GetPayloadLength(
    std::span<const uint8_t> buffer) noexcept {
  if (buffer.size() < HEADER_SIZE) {
    return ParseError::InsufficientData;
  }

  return static_cast<uint16_t>(buffer[4] | (buffer[5] << 8));
}

bool FrameParser::ValidateCrc(std::span<const uint8_t> buffer) noexcept {
  if (buffer.size() < MIN_FRAME_SIZE) {
    return false;
  }

  auto payload_len_result = GetPayloadLength(buffer);
  if (IsError(payload_len_result)) {
    return false;
  }

  uint16_t payload_len = GetValue(payload_len_result);
  size_t frame_size = HEADER_SIZE + payload_len + CRC_SIZE;

  if (buffer.size() < frame_size) {
    return false;
  }

  uint16_t recv_crc = buffer[HEADER_SIZE + payload_len] |
                      (buffer[HEADER_SIZE + payload_len + 1] << 8);
  uint16_t calc_crc =
      Protocol::CalculateCrc16(buffer.subspan(2, 4 + payload_len));

  return recv_crc == calc_crc;
}

int FrameParser::FindFrameStart(std::span<const uint8_t> buffer) noexcept {
  if (buffer.size() < 2) {
    return -1;
  }

  for (size_t i = 0; i < buffer.size() - 1; i++) {
    if (buffer[i] == FRAME_PREFIX_0 && buffer[i + 1] == FRAME_PREFIX_1) {
      return static_cast<int>(i);
    }
  }

  return -1;
}

// ═══════════════════════════════════════════════════════════════════════════
// Protocol - основной API
// ═══════════════════════════════════════════════════════════════════════════

uint16_t Protocol::CalculateCrc16(std::span<const uint8_t> data) noexcept {
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

// ─────────────────────────────────────────────────────────────────────────
// Сборка кадров
// ─────────────────────────────────────────────────────────────────────────

Result<size_t> Protocol::BuildTelemetry(std::span<uint8_t> buffer,
                                        const TelemetryData& data) noexcept {
  // Сериализуем payload
  std::array<uint8_t, TelemetryData::PAYLOAD_SIZE> payload{};
  payload[0] = data.seq & 0xFF;
  payload[1] = (data.seq >> 8) & 0xFF;
  payload[2] = data.status;
  payload[3] = data.ax & 0xFF;
  payload[4] = (data.ax >> 8) & 0xFF;
  payload[5] = data.ay & 0xFF;
  payload[6] = (data.ay >> 8) & 0xFF;
  payload[7] = data.az & 0xFF;
  payload[8] = (data.az >> 8) & 0xFF;
  payload[9] = data.gx & 0xFF;
  payload[10] = (data.gx >> 8) & 0xFF;
  payload[11] = data.gy & 0xFF;
  payload[12] = (data.gy >> 8) & 0xFF;
  payload[13] = data.gz & 0xFF;
  payload[14] = (data.gz >> 8) & 0xFF;

  FrameBuilder builder(MessageType::Telemetry);
  return builder.Build(buffer, payload);
}

Result<size_t> Protocol::BuildCommand(std::span<uint8_t> buffer,
                                      const CommandData& data) noexcept {
  // Ограничиваем значения
  CommandData clamped = data.Clamped();

  // Сериализуем payload
  std::array<uint8_t, CommandData::PAYLOAD_SIZE> payload{};
  payload[0] = next_command_seq_ & 0xFF;
  payload[1] = (next_command_seq_ >> 8) & 0xFF;
  next_command_seq_++;

  int16_t thr_i16 = static_cast<int16_t>(clamped.throttle * 32767.0f);
  int16_t steer_i16 = static_cast<int16_t>(clamped.steering * 32767.0f);

  payload[2] = thr_i16 & 0xFF;
  payload[3] = (thr_i16 >> 8) & 0xFF;
  payload[4] = steer_i16 & 0xFF;
  payload[5] = (steer_i16 >> 8) & 0xFF;
  payload[6] = 0;  // reserved

  FrameBuilder builder(MessageType::Command);
  return builder.Build(buffer, payload);
}

Result<size_t> Protocol::BuildLog(std::span<uint8_t> buffer,
                                  std::string_view msg) noexcept {
  size_t msg_len = msg.size();
  if (msg_len > LOG_MAX_PAYLOAD) {
    msg_len = LOG_MAX_PAYLOAD;
  }

  std::span<const uint8_t> payload(reinterpret_cast<const uint8_t*>(msg.data()),
                                   msg_len);

  FrameBuilder builder(MessageType::Log);
  return builder.Build(buffer, payload);
}

Result<size_t> Protocol::BuildPing(std::span<uint8_t> buffer) noexcept {
  FrameBuilder builder(MessageType::Ping);
  return builder.Build(buffer, std::span<const uint8_t>());
}

Result<size_t> Protocol::BuildPong(std::span<uint8_t> buffer) noexcept {
  FrameBuilder builder(MessageType::Pong);
  return builder.Build(buffer, std::span<const uint8_t>());
}

// ─────────────────────────────────────────────────────────────────────────
// Парсинг кадров
// ─────────────────────────────────────────────────────────────────────────

Result<TelemetryData> Protocol::ParseTelemetry(
    std::span<const uint8_t> buffer) noexcept {
  // Валидация заголовка
  auto type_result = FrameParser::ValidateHeader(buffer);
  if (IsError(type_result)) {
    return GetError(type_result);
  }

  if (GetValue(type_result) != MessageType::Telemetry) {
    return ParseError::InvalidType;
  }

  // Проверка длины payload
  auto payload_len_result = FrameParser::GetPayloadLength(buffer);
  if (IsError(payload_len_result)) {
    return GetError(payload_len_result);
  }

  uint16_t payload_len = GetValue(payload_len_result);
  if (payload_len != TelemetryData::PAYLOAD_SIZE) {
    return ParseError::InvalidPayloadLength;
  }

  size_t frame_size = HEADER_SIZE + payload_len + CRC_SIZE;
  if (buffer.size() < frame_size) {
    return ParseError::InsufficientData;
  }

  // Проверка CRC
  if (!FrameParser::ValidateCrc(buffer)) {
    return ParseError::CrcMismatch;
  }

  // Десериализация
  TelemetryData data;
  data.seq = buffer[6] | (buffer[7] << 8);
  data.status = buffer[8];
  data.ax = static_cast<int16_t>(buffer[9] | (buffer[10] << 8));
  data.ay = static_cast<int16_t>(buffer[11] | (buffer[12] << 8));
  data.az = static_cast<int16_t>(buffer[13] | (buffer[14] << 8));
  data.gx = static_cast<int16_t>(buffer[15] | (buffer[16] << 8));
  data.gy = static_cast<int16_t>(buffer[17] | (buffer[18] << 8));
  data.gz = static_cast<int16_t>(buffer[19] | (buffer[20] << 8));

  return data;
}

Result<CommandData> Protocol::ParseCommand(
    std::span<const uint8_t> buffer) noexcept {
  // Валидация заголовка
  auto type_result = FrameParser::ValidateHeader(buffer);
  if (IsError(type_result)) {
    return GetError(type_result);
  }

  if (GetValue(type_result) != MessageType::Command) {
    return ParseError::InvalidType;
  }

  // Проверка длины payload
  auto payload_len_result = FrameParser::GetPayloadLength(buffer);
  if (IsError(payload_len_result)) {
    return GetError(payload_len_result);
  }

  uint16_t payload_len = GetValue(payload_len_result);
  if (payload_len != CommandData::PAYLOAD_SIZE) {
    return ParseError::InvalidPayloadLength;
  }

  size_t frame_size = HEADER_SIZE + payload_len + CRC_SIZE;
  if (buffer.size() < frame_size) {
    return ParseError::InsufficientData;
  }

  // Проверка CRC
  if (!FrameParser::ValidateCrc(buffer)) {
    return ParseError::CrcMismatch;
  }

  // Десериализация
  CommandData data;
  data.seq = buffer[6] | (buffer[7] << 8);

  int16_t thr_i16 = static_cast<int16_t>(buffer[8] | (buffer[9] << 8));
  int16_t steer_i16 = static_cast<int16_t>(buffer[10] | (buffer[11] << 8));

  data.throttle = static_cast<float>(thr_i16) / 32767.0f;
  data.steering = static_cast<float>(steer_i16) / 32767.0f;

  // Ограничиваем значения (защита от -32768 и выбросов)
  return data.Clamped();
}

Result<std::string_view> Protocol::ParseLog(
    std::span<const uint8_t> buffer) noexcept {
  // Валидация заголовка
  auto type_result = FrameParser::ValidateHeader(buffer);
  if (IsError(type_result)) {
    return GetError(type_result);
  }

  if (GetValue(type_result) != MessageType::Log) {
    return ParseError::InvalidType;
  }

  // Проверка длины payload
  auto payload_len_result = FrameParser::GetPayloadLength(buffer);
  if (IsError(payload_len_result)) {
    return GetError(payload_len_result);
  }

  uint16_t payload_len = GetValue(payload_len_result);
  if (payload_len > LOG_MAX_PAYLOAD) {
    return ParseError::InvalidPayloadLength;
  }

  size_t frame_size = HEADER_SIZE + payload_len + CRC_SIZE;
  if (buffer.size() < frame_size) {
    return ParseError::InsufficientData;
  }

  // Проверка CRC
  if (!FrameParser::ValidateCrc(buffer)) {
    return ParseError::CrcMismatch;
  }

  // Возвращаем view на payload
  const char* msg_ptr =
      reinterpret_cast<const char*>(buffer.data() + HEADER_SIZE);
  return std::string_view(msg_ptr, payload_len);
}

Result<bool> Protocol::ParsePing(std::span<const uint8_t> buffer) noexcept {
  // Валидация заголовка
  auto type_result = FrameParser::ValidateHeader(buffer);
  if (IsError(type_result)) {
    return GetError(type_result);
  }

  if (GetValue(type_result) != MessageType::Ping) {
    return ParseError::InvalidType;
  }

  // Проверка длины payload (должна быть 0)
  auto payload_len_result = FrameParser::GetPayloadLength(buffer);
  if (IsError(payload_len_result)) {
    return GetError(payload_len_result);
  }

  if (GetValue(payload_len_result) != 0) {
    return ParseError::InvalidPayloadLength;
  }

  if (buffer.size() < MIN_FRAME_SIZE) {
    return ParseError::InsufficientData;
  }

  // Проверка CRC
  if (!FrameParser::ValidateCrc(buffer)) {
    return ParseError::CrcMismatch;
  }

  return true;
}

Result<bool> Protocol::ParsePong(std::span<const uint8_t> buffer) noexcept {
  // Валидация заголовка
  auto type_result = FrameParser::ValidateHeader(buffer);
  if (IsError(type_result)) {
    return GetError(type_result);
  }

  if (GetValue(type_result) != MessageType::Pong) {
    return ParseError::InvalidType;
  }

  // Проверка длины payload (должна быть 0)
  auto payload_len_result = FrameParser::GetPayloadLength(buffer);
  if (IsError(payload_len_result)) {
    return GetError(payload_len_result);
  }

  if (GetValue(payload_len_result) != 0) {
    return ParseError::InvalidPayloadLength;
  }

  if (buffer.size() < MIN_FRAME_SIZE) {
    return ParseError::InsufficientData;
  }

  // Проверка CRC
  if (!FrameParser::ValidateCrc(buffer)) {
    return ParseError::CrcMismatch;
  }

  return true;
}

}  // namespace rc_vehicle::protocol
