#include "uart_bridge_base.hpp"
#include <cstring>

void UartBridgeBase::PumpRx() {
  int n = ReadAvailable(rx_buffer_ + rx_pos_, RX_BUF_SIZE - rx_pos_);
  if (n > 0)
    rx_pos_ += static_cast<size_t>(n);
}

int UartBridgeBase::SendTelem(const TelemetryData &telem_data) {
  uint8_t frame[32];
  size_t len = ProtocolBuildTelem(frame, sizeof(frame), &telem_data);
  if (len == 0)
    return -1;
  return Write(frame, len);
}

std::optional<UartCommand> UartBridgeBase::ReceiveCommand() {
  PumpRx();
  int start = ProtocolFindFrameStart(rx_buffer_, rx_pos_);
  if (start < 0) {
    if (rx_pos_ >= RX_BUF_SIZE - 1)
      rx_pos_ = 0;
    return std::nullopt;
  }
  if (start > 0) {
    memmove(rx_buffer_, rx_buffer_ + start, rx_pos_ - start);
    rx_pos_ -= start;
  }
  if (rx_pos_ < 16)
    return std::nullopt;
  float throttle = 0.f, steering = 0.f;
  size_t parsed =
      ProtocolParseCommand(rx_buffer_, rx_pos_, &throttle, &steering);
  if (parsed > 0) {
    memmove(rx_buffer_, rx_buffer_ + parsed, rx_pos_ - parsed);
    rx_pos_ -= parsed;
    return UartCommand{throttle, steering};
  }
  if (rx_pos_ > 0) {
    memmove(rx_buffer_, rx_buffer_ + 1, rx_pos_ - 1);
    rx_pos_--;
  }
  return std::nullopt;
}

int UartBridgeBase::SendCommand(float throttle, float steering) {
  uint8_t frame[32];
  size_t len = ProtocolBuildCommand(frame, sizeof(frame), throttle, steering);
  if (len == 0)
    return -1;
  return Write(frame, len);
}

int UartBridgeBase::ReceiveTelem(TelemetryData *telem_data) {
  if (telem_data == nullptr)
    return -1;
  PumpRx();
  int start = ProtocolFindFrameStart(rx_buffer_, rx_pos_);
  if (start < 0) {
    if (rx_pos_ >= RX_BUF_SIZE - 1)
      rx_pos_ = 0;
    return -1;
  }
  if (start > 0) {
    memmove(rx_buffer_, rx_buffer_ + start, rx_pos_ - start);
    rx_pos_ -= start;
  }
  if (rx_pos_ < 20)
    return -1;
  size_t parsed = ProtocolParseTelem(rx_buffer_, rx_pos_, telem_data);
  if (parsed > 0) {
    memmove(rx_buffer_, rx_buffer_ + parsed, rx_pos_ - parsed);
    rx_pos_ -= parsed;
    return 0;
  }
  if (rx_pos_ > 0) {
    memmove(rx_buffer_, rx_buffer_ + 1, rx_pos_ - 1);
    rx_pos_--;
  }
  return -1;
}
