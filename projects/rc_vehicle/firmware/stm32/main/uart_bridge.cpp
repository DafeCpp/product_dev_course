#include "uart_bridge.hpp"

#include "protocol.hpp"
#include "uart_bridge_base.hpp"
// TODO: реализовать Init/Write/ReadAvailable на STM32Cube LL (USART, пины в
// board_pins.hpp)

class Stm32UartBridge : public UartBridgeBase {
 public:
  int Init() override {
    // TODO: LL: RCC enable USART+GPIO, GPIO AF, USART baud, enable
    rx_pos_ = 0;
    return 0;
  }

  int Write(const uint8_t *data, size_t len) override {
    if (data == nullptr) return -1;
    // TODO: LL_USART_TransmitData8 в цикле
    (void)len;
    return 0;
  }

  int ReadAvailable(uint8_t *buf, size_t max_len) override {
    if (buf == nullptr || max_len == 0) return 0;
    // TODO: LL_USART_IsActiveFlag_RXNE + ReceiveData8
    return 0;
  }
};

static Stm32UartBridge s_bridge;

int UartBridgeInit(void) { return s_bridge.Init(); }

int UartBridgeSendTelem(const TelemetryData &telem_data) {
  return s_bridge.SendTelem(telem_data);
}

std::optional<UartBridgeCommand> UartBridgeReceiveCommand(void) {
  auto cmd = s_bridge.ReceiveCommand();
  if (cmd) {
    return UartBridgeCommand{cmd->throttle, cmd->steering};
  }
  return std::nullopt;
}

bool UartBridgeReceivePing(void) { return s_bridge.ReceivePing(); }

int UartBridgeSendPong(void) { return s_bridge.SendPong(); }
