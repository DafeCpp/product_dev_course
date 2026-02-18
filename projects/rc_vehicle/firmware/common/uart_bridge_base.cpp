#include "uart_bridge_base.hpp"

#include <cstring>

namespace rc_vehicle {

// ═══════════════════════════════════════════════════════════════════════════
// RxBuffer - реализация
// ═══════════════════════════════════════════════════════════════════════════

void RxBuffer::Consume(size_t n) noexcept {
  if (n == 0 || n > pos_) return;

  std::memmove(data_.data(), data_.data() + n, pos_ - n);
  pos_ -= n;
}

bool RxBuffer::Align() noexcept {
  int start = protocol::Protocol::FindFrameStart(Data());

  if (start < 0) {
    // Нет AA 55 — если буфер почти полон, сбросить
    if (IsFull()) {
      Reset();
    }
    return false;
  }

  if (start > 0) {
    Consume(static_cast<size_t>(start));
  }

  return true;
}

void RxBuffer::SkipOne() noexcept {
  if (pos_ > 0) {
    Consume(1);
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// UartBridgeBase - реализация
// ═══════════════════════════════════════════════════════════════════════════

void UartBridgeBase::PumpRx() {
  auto available = rx_buffer_.Available();
  int n = ReadAvailable(available.data(), available.size());
  if (n > 0) {
    rx_buffer_.Advance(static_cast<size_t>(n));
  }
}

// ─────────────────────────────────────────────────────────────────────────
// Шаблонный метод для приёма кадров (устраняет дублирование)
// ─────────────────────────────────────────────────────────────────────────

template <typename T>
std::optional<T> UartBridgeBase::ReceiveFrame(
    protocol::MessageType expected_type,
    protocol::Result<T> (*parse_func)(std::span<const uint8_t>)) {
  PumpRx();

  // Выравниваем буфер (ищем AA 55)
  if (!rx_buffer_.Align()) {
    return std::nullopt;
  }

  auto data = rx_buffer_.Data();

  // Проверяем минимальный размер для заголовка
  if (data.size() < 4) {
    return std::nullopt;
  }

  // Проверяем тип сообщения
  auto type_result = protocol::FrameParser::ValidateHeader(data);
  if (protocol::IsError(type_result)) {
    return std::nullopt;
  }

  if (protocol::GetValue(type_result) != expected_type) {
    // Чужой кадр — не трогаем, вернём nullopt
    return std::nullopt;
  }

  // Пытаемся распарсить
  auto parse_result = parse_func(data);

  if (protocol::IsOk(parse_result)) {
    // Успешно распарсили — потребляем кадр
    auto payload_len_result = protocol::FrameParser::GetPayloadLength(data);
    if (protocol::IsOk(payload_len_result)) {
      size_t frame_size = protocol::HEADER_SIZE +
                          protocol::GetValue(payload_len_result) +
                          protocol::CRC_SIZE;
      rx_buffer_.Consume(frame_size);
    }
    return protocol::GetValue(parse_result);
  }

  // Ошибка парсинга (вероятно CRC) — пропускаем 1 байт (ложный AA 55)
  rx_buffer_.SkipOne();
  return std::nullopt;
}

// ─────────────────────────────────────────────────────────────────────────
// API для MCU: отправка телеметрии, приём команд
// ─────────────────────────────────────────────────────────────────────────

int UartBridgeBase::SendTelem(const protocol::TelemetryData &telem_data) {
  std::array<uint8_t, 32> frame{};
  auto result = protocol::Protocol::BuildTelemetry(frame, telem_data);

  if (protocol::IsError(result)) {
    return -1;
  }

  size_t len = protocol::GetValue(result);
  return Write(frame.data(), len);
}

std::optional<UartCommand> UartBridgeBase::ReceiveCommand() {
  auto result = ReceiveFrame<protocol::CommandData>(
      protocol::MessageType::Command, protocol::Protocol::ParseCommand);

  if (!result.has_value()) {
    return std::nullopt;
  }

  const auto &cmd = *result;
  return UartCommand{cmd.throttle, cmd.steering};
}

bool UartBridgeBase::ReceivePing() {
  auto result = ReceiveFrame<bool>(protocol::MessageType::Ping,
                                   protocol::Protocol::ParsePing);

  return result.has_value();
}

int UartBridgeBase::SendPong() {
  std::array<uint8_t, 16> frame{};
  auto result = protocol::Protocol::BuildPong(frame);

  if (protocol::IsError(result)) {
    return -1;
  }

  size_t len = protocol::GetValue(result);
  return Write(frame.data(), len);
}

int UartBridgeBase::SendLog(const char *msg, size_t len) {
  std::array<uint8_t, protocol::HEADER_SIZE + protocol::LOG_MAX_PAYLOAD +
                          protocol::CRC_SIZE>
      frame{};
  auto result = protocol::Protocol::BuildLog(frame, std::string_view(msg, len));

  if (protocol::IsError(result)) {
    return -1;
  }

  size_t frame_len = protocol::GetValue(result);
  return Write(frame.data(), frame_len);
}

// ─────────────────────────────────────────────────────────────────────────
// API для ESP32: отправка команд, приём телеметрии
// ─────────────────────────────────────────────────────────────────────────

int UartBridgeBase::SendCommand(float throttle, float steering) {
  std::array<uint8_t, 32> frame{};
  protocol::CommandData cmd{0, throttle, steering};
  auto result = protocol::Protocol::BuildCommand(frame, cmd);

  if (protocol::IsError(result)) {
    return -1;
  }

  size_t len = protocol::GetValue(result);
  return Write(frame.data(), len);
}

std::optional<protocol::TelemetryData> UartBridgeBase::ReceiveTelem() {
  return ReceiveFrame<protocol::TelemetryData>(
      protocol::MessageType::Telemetry, protocol::Protocol::ParseTelemetry);
}

int UartBridgeBase::ReceiveLog(char *buf, size_t max_len) {
  auto result = ReceiveFrame<std::string_view>(protocol::MessageType::Log,
                                               protocol::Protocol::ParseLog);

  if (!result.has_value()) {
    return 0;
  }

  const auto &log_msg = *result;
  size_t copy_len = log_msg.size() < max_len ? log_msg.size() : max_len;
  std::memcpy(buf, log_msg.data(), copy_len);

  return static_cast<int>(copy_len);
}

int UartBridgeBase::SendPing() {
  std::array<uint8_t, 16> frame{};
  auto result = protocol::Protocol::BuildPing(frame);

  if (protocol::IsError(result)) {
    return -1;
  }

  size_t len = protocol::GetValue(result);
  return Write(frame.data(), len);
}

bool UartBridgeBase::ReceivePong() {
  auto result = ReceiveFrame<bool>(protocol::MessageType::Pong,
                                   protocol::Protocol::ParsePong);

  return result.has_value();
}

}  // namespace rc_vehicle
