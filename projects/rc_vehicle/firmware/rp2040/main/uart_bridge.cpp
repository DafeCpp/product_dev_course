#include "uart_bridge.hpp"

#include "config.hpp"
#include "hardware/uart.h"
#include "pico/stdlib.h"
#include "uart_bridge_base.hpp"

class Rp2040UartBridge : public UartBridgeBase {
 public:
  int Init() override {
    uart_init(UART_ID, UART_BAUD_RATE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    rx_pos_ = 0;
    return 0;
  }

  int Write(const uint8_t *data, size_t len) override {
    if (data == nullptr) return -1;
    uart_write_blocking(UART_ID, data, len);
    return 0;
  }

  int ReadAvailable(uint8_t *buf, size_t max_len) override {
    if (buf == nullptr || max_len == 0) return 0;
    size_t n = 0;
    while (n < max_len && uart_is_readable(UART_ID)) {
      buf[n++] = static_cast<uint8_t>(uart_getc(UART_ID));
    }
    return static_cast<int>(n);
  }
};

static Rp2040UartBridge s_bridge;

int UartBridgeInit(void) { return s_bridge.Init(); }

int UartBridgeSendTelem(const TelemetryData &telem_data) {
  return s_bridge.SendTelem(telem_data);
}

std::optional<UartBridgeCommand> UartBridgeReceiveCommand() {
  auto cmd = s_bridge.ReceiveCommand();
  if (cmd) return UartBridgeCommand{cmd->throttle, cmd->steering};
  return std::nullopt;
}

bool UartBridgeReceivePing(void) { return s_bridge.ReceivePing(); }

int UartBridgeSendPong(void) { return s_bridge.SendPong(); }
