#include "uart_bridge_base.hpp"

#include <cstring>
#include <span>

void UartBridgeBase::PumpRx() {
  int n = ReadAvailable(rx_buffer_.data() + rx_pos_, RX_BUF_SIZE - rx_pos_);
  if (n > 0) rx_pos_ += static_cast<size_t>(n);
}

/**
 * Общий хелпер: PumpRx → найти AA 55 → выровнять буфер → подглядеть тип.
 * Возвращает тип сообщения (0x01–0x05…) или -1 если кадра нет / мало данных.
 */
static int AlignAndPeekType(std::array<uint8_t, UartBridgeBase::RX_BUF_SIZE> &buf,
                            size_t &pos) {
  std::span<const uint8_t> rx_span(buf.data(), pos);
  int start = ProtocolFindFrameStart(rx_span);
  if (start < 0) {
    // Нет AA 55 — если буфер почти полон, сбросить
    if (pos >= UartBridgeBase::RX_BUF_SIZE - 1) pos = 0;
    return -1;
  }
  if (start > 0) {
    memmove(buf.data(), buf.data() + start, pos - start);
    pos -= static_cast<size_t>(start);
  }
  // Нужно минимум 4 байта: prefix(2) + ver(1) + type(1)
  if (pos < 4) return -1;
  return buf[3];
}

/** Сдвинуть буфер на 1 байт (пропуск ложного AA 55). */
static void SkipOneByte(std::array<uint8_t, UartBridgeBase::RX_BUF_SIZE> &buf,
                        size_t &pos) {
  if (pos > 0) {
    memmove(buf.data(), buf.data() + 1, pos - 1);
    pos--;
  }
}

/** Потребить parsed байт из начала буфера. */
static void ConsumeFront(std::array<uint8_t, UartBridgeBase::RX_BUF_SIZE> &buf,
                         size_t &pos, size_t parsed) {
  if (parsed > 0 && parsed <= pos) {
    memmove(buf.data(), buf.data() + parsed, pos - parsed);
    pos -= parsed;
  }
}

// ── MCU → ESP32: телеметрия ─────────────────────────────────────────────────

int UartBridgeBase::SendTelem(const TelemetryData &telem_data) {
  std::array<uint8_t, 32> frame{};
  size_t len = ProtocolBuildTelem(frame, telem_data);
  if (len == 0) return -1;
  return Write(frame.data(), len);
}

std::optional<UartCommand> UartBridgeBase::ReceiveCommand() {
  PumpRx();
  int type = AlignAndPeekType(rx_buffer_, rx_pos_);
  if (type < 0) return std::nullopt;
  if (type != UART_MSG_TYPE_COMMAND) return std::nullopt;  // чужой кадр — не трогаем

  if (rx_pos_ < 16) return std::nullopt;
  float throttle = 0.f, steering = 0.f;
  size_t parsed = ProtocolParseCommand(
      std::span<const uint8_t>(rx_buffer_.data(), rx_pos_), throttle, steering);
  if (parsed > 0) {
    ConsumeFront(rx_buffer_, rx_pos_, parsed);
    return UartCommand{throttle, steering};
  }
  // Наш тип, но CRC не сошлась — ложный AA 55, пропускаем 1 байт
  SkipOneByte(rx_buffer_, rx_pos_);
  return std::nullopt;
}

// ── MCU: PING/PONG ─────────────────────────────────────────────────────────

bool UartBridgeBase::ReceivePing() {
  PumpRx();
  int type = AlignAndPeekType(rx_buffer_, rx_pos_);
  if (type < 0) return false;
  if (type != UART_MSG_TYPE_PING) return false;

  if (rx_pos_ < 8) return false;
  size_t parsed =
      ProtocolParsePing(std::span<const uint8_t>(rx_buffer_.data(), rx_pos_));
  if (parsed > 0) {
    ConsumeFront(rx_buffer_, rx_pos_, parsed);
    return true;
  }
  SkipOneByte(rx_buffer_, rx_pos_);
  return false;
}

int UartBridgeBase::SendPong() {
  std::array<uint8_t, 16> frame{};
  size_t len = ProtocolBuildPong(frame);
  if (len == 0) return -1;
  return Write(frame.data(), len);
}

// ── MCU → ESP32: LOG ────────────────────────────────────────────────────────

int UartBridgeBase::SendLog(const char *msg, size_t len) {
  std::array<uint8_t, 6 + PROTOCOL_LOG_MAX_PAYLOAD + 2> frame{};
  size_t frame_len = ProtocolBuildLog(frame, msg, len);
  if (frame_len == 0) return -1;
  return Write(frame.data(), frame_len);
}

// ── ESP32: отправка команд, приём телеметрии ────────────────────────────────

int UartBridgeBase::SendCommand(float throttle, float steering) {
  std::array<uint8_t, 32> frame{};
  size_t len = ProtocolBuildCommand(frame, throttle, steering);
  if (len == 0) return -1;
  return Write(frame.data(), len);
}

std::optional<TelemetryData> UartBridgeBase::ReceiveTelem() {
  PumpRx();
  int type = AlignAndPeekType(rx_buffer_, rx_pos_);
  if (type < 0) return std::nullopt;
  if (type != UART_MSG_TYPE_TELEM) return std::nullopt;

  if (rx_pos_ < 20) return std::nullopt;
  TelemetryData telem_data{};
  size_t parsed = ProtocolParseTelem(
      std::span<const uint8_t>(rx_buffer_.data(), rx_pos_), telem_data);
  if (parsed > 0) {
    ConsumeFront(rx_buffer_, rx_pos_, parsed);
    return telem_data;
  }
  SkipOneByte(rx_buffer_, rx_pos_);
  return std::nullopt;
}

int UartBridgeBase::ReceiveLog(char *buf, size_t max_len) {
  PumpRx();
  int type = AlignAndPeekType(rx_buffer_, rx_pos_);
  if (type < 0) return 0;
  if (type != UART_MSG_TYPE_LOG) return 0;

  if (rx_pos_ < 8) return 0;
  size_t msg_len = 0;
  size_t parsed = ProtocolParseLog(
      std::span<const uint8_t>(rx_buffer_.data(), rx_pos_), buf, max_len,
      msg_len);
  if (parsed > 0) {
    ConsumeFront(rx_buffer_, rx_pos_, parsed);
    return static_cast<int>(msg_len);
  }
  SkipOneByte(rx_buffer_, rx_pos_);
  return 0;
}

// ── ESP32: PING/PONG ───────────────────────────────────────────────────────

int UartBridgeBase::SendPing() {
  std::array<uint8_t, 16> frame{};
  size_t len = ProtocolBuildPing(frame);
  if (len == 0) return -1;
  return Write(frame.data(), len);
}

bool UartBridgeBase::ReceivePong() {
  PumpRx();
  int type = AlignAndPeekType(rx_buffer_, rx_pos_);
  if (type < 0) return false;
  if (type != UART_MSG_TYPE_PONG) return false;

  if (rx_pos_ < 8) return false;
  size_t parsed =
      ProtocolParsePong(std::span<const uint8_t>(rx_buffer_.data(), rx_pos_));
  if (parsed > 0) {
    ConsumeFront(rx_buffer_, rx_pos_, parsed);
    return true;
  }
  SkipOneByte(rx_buffer_, rx_pos_);
  return false;
}
