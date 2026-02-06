#include "uart_bridge_base.hpp"

#include <cstring>
#include <span>

void UartBridgeBase::PumpRx() {
  int n = ReadAvailable(rx_buffer_.data() + rx_pos_, RX_BUF_SIZE - rx_pos_);
  if (n > 0) rx_pos_ += static_cast<size_t>(n);
}

int UartBridgeBase::SendTelem(const TelemetryData &telem_data) {
  std::array<uint8_t, 32> frame{};
  size_t len = ProtocolBuildTelem(frame, telem_data);
  if (len == 0) return -1;
  return Write(frame);
}

std::optional<UartCommand> UartBridgeBase::ReceiveCommand() {
  PumpRx();
  std::span<const uint8_t> rx_span(rx_buffer_.data(), rx_pos_);
  int start = ProtocolFindFrameStart(rx_span);
  if (start < 0) {
    if (rx_pos_ >= RX_BUF_SIZE - 1) rx_pos_ = 0;
    return std::nullopt;
  }
  if (start > 0) {
    memmove(rx_buffer_.data(), rx_buffer_.data() + start, rx_pos_ - start);
    rx_pos_ -= start;
  }
  if (rx_pos_ < 16) return std::nullopt;
  float throttle = 0.f, steering = 0.f;
  size_t parsed = ProtocolParseCommand(
      std::span<const uint8_t>(rx_buffer_.data(), rx_pos_), throttle, steering);
  if (parsed > 0) {
    memmove(rx_buffer_.data(), rx_buffer_.data() + parsed, rx_pos_ - parsed);
    rx_pos_ -= parsed;
    return UartCommand{throttle, steering};
  }
  if (rx_pos_ > 0) {
    memmove(rx_buffer_.data(), rx_buffer_.data() + 1, rx_pos_ - 1);
    rx_pos_--;
  }
  return std::nullopt;
}

int UartBridgeBase::SendCommand(float throttle, float steering) {
  std::array<uint8_t, 32> frame{};
  size_t len = ProtocolBuildCommand(frame, throttle, steering);
  if (len == 0) return -1;
  return Write(frame.data(), len);
}

std::optional<TelemetryData> UartBridgeBase::ReceiveTelem() {
  PumpRx();
  std::span<const uint8_t> rx_span(rx_buffer_.data(), rx_pos_);
  int start = ProtocolFindFrameStart(rx_span);
  if (start < 0) {
    if (rx_pos_ >= RX_BUF_SIZE - 1) rx_pos_ = 0;
    return std::nullopt;
  }
  if (start > 0) {
    memmove(rx_buffer_.data(), rx_buffer_.data() + start, rx_pos_ - start);
    rx_pos_ -= start;
  }
  if (rx_pos_ < 20) return std::nullopt;
  TelemetryData telem_data{};
  size_t parsed = ProtocolParseTelem(
      std::span<const uint8_t>(rx_buffer_.data(), rx_pos_), telem_data);
  if (parsed > 0) {
    memmove(rx_buffer_.data(), rx_buffer_.data() + parsed, rx_pos_ - parsed);
    rx_pos_ -= parsed;
    return telem_data;
  }
  if (rx_pos_ > 0) {
    memmove(rx_buffer_.data(), rx_buffer_.data() + 1, rx_pos_ - 1);
    rx_pos_--;
  }
  return std::nullopt;
}

bool UartBridgeBase::ReceivePing() {
  PumpRx();
  std::span<const uint8_t> rx_span(rx_buffer_.data(), rx_pos_);
  int start = ProtocolFindFrameStart(rx_span);
  if (start < 0) {
    if (rx_pos_ >= RX_BUF_SIZE - 1) rx_pos_ = 0;
    return false;
  }
  if (start > 0) {
    memmove(rx_buffer_.data(), rx_buffer_.data() + start, rx_pos_ - start);
    rx_pos_ -= start;
  }
  if (rx_pos_ < 8) return false;
  size_t parsed =
      ProtocolParsePing(std::span<const uint8_t>(rx_buffer_.data(), rx_pos_));
  if (parsed > 0) {
    memmove(rx_buffer_.data(), rx_buffer_.data() + parsed, rx_pos_ - parsed);
    rx_pos_ -= parsed;
    return true;
  }
  if (rx_pos_ > 0) {
    memmove(rx_buffer_.data(), rx_buffer_.data() + 1, rx_pos_ - 1);
    rx_pos_--;
  }
  return false;
}

int UartBridgeBase::SendPong() {
  std::array<uint8_t, 16> frame{};
  size_t len = ProtocolBuildPong(frame);
  if (len == 0) return -1;
  return Write(frame.data(), len);
}

int UartBridgeBase::SendPing() {
  std::array<uint8_t, 16> frame{};
  size_t len = ProtocolBuildPing(frame);
  if (len == 0) return -1;
  return Write(frame.data(), len);
}

bool UartBridgeBase::ReceivePong() {
  PumpRx();
  std::span<const uint8_t> rx_span(rx_buffer_.data(), rx_pos_);
  int start = ProtocolFindFrameStart(rx_span);
  if (start < 0) {
    if (rx_pos_ >= RX_BUF_SIZE - 1) rx_pos_ = 0;
    return false;
  }
  if (start > 0) {
    memmove(rx_buffer_.data(), rx_buffer_.data() + start, rx_pos_ - start);
    rx_pos_ -= start;
  }
  if (rx_pos_ < 8) return false;
  size_t parsed =
      ProtocolParsePong(std::span<const uint8_t>(rx_buffer_.data(), rx_pos_));
  if (parsed > 0) {
    memmove(rx_buffer_.data(), rx_buffer_.data() + parsed, rx_pos_ - parsed);
    rx_pos_ -= parsed;
    return true;
  }
  if (rx_pos_ > 0) {
    memmove(rx_buffer_.data(), rx_buffer_.data() + 1, rx_pos_ - 1);
    rx_pos_--;
  }
  return false;
}
