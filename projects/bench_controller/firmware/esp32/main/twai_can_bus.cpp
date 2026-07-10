#include "twai_can_bus.hpp"

#include <cstring>

#include "esp_log.h"
#include "esp_twai_onchip.h"

namespace bench {

namespace {
constexpr const char* kTag = "twai";
constexpr uint32_t kRxQueueDepth = 64;
constexpr uint32_t kTimestampHz = 1'000'000;  // 1 мкс на тик
}  // namespace

TwaiCanBus::~TwaiCanBus() {
  if (node_ != nullptr) {
    twai_node_disable(node_);
    twai_node_delete(node_);
  }
  if (rx_queue_ != nullptr) {
    vQueueDelete(rx_queue_);
  }
}

bool TwaiCanBus::OnRxDone(twai_node_handle_t handle,
                          const twai_rx_done_event_data_t* edata, void* ctx) {
  (void)edata;
  auto* self = static_cast<TwaiCanBus*>(ctx);

  uint8_t data[8] = {0};
  twai_frame_t rx = {};
  rx.buffer = data;
  rx.buffer_len = sizeof(data);
  if (twai_node_receive_from_isr(handle, &rx) != ESP_OK) {
    return false;
  }

  TwaiRxItem item = {};
  item.frame.id = rx.header.id;
  item.frame.dlc = static_cast<uint8_t>(rx.header.dlc);
  const size_t n = rx.header.dlc > 8 ? 8 : rx.header.dlc;
  std::memcpy(item.frame.data, data, n);
  item.timestamp = rx.header.timestamp;

  BaseType_t hp_task_woken = pdFALSE;
  xQueueSendFromISR(self->rx_queue_, &item, &hp_task_woken);
  if (self->tap_ != nullptr) {
    BaseType_t hp2 = pdFALSE;
    xQueueSendFromISR(self->tap_, &item, &hp2);
    hp_task_woken = hp_task_woken || hp2;
  }
  return hp_task_woken == pdTRUE;
}

bool TwaiCanBus::Init(int gpio_tx, int gpio_rx, uint32_t bitrate) {
  rx_queue_ = xQueueCreate(kRxQueueDepth, sizeof(TwaiRxItem));
  if (rx_queue_ == nullptr) {
    return false;
  }

  twai_onchip_node_config_t cfg = {};
  cfg.io_cfg.tx = static_cast<gpio_num_t>(gpio_tx);
  cfg.io_cfg.rx = static_cast<gpio_num_t>(gpio_rx);
  cfg.io_cfg.quanta_clk_out = static_cast<gpio_num_t>(-1);
  cfg.io_cfg.bus_off_indicator = static_cast<gpio_num_t>(-1);
  cfg.bit_timing.bitrate = bitrate;
  cfg.timestamp_resolution_hz = kTimestampHz;
  cfg.tx_queue_depth = 32;
  // self_test: TX без ACK (второго узла на шине физически нет);
  // loopback: контроллер слышит собственные кадры → мастер и эмулятор
  // на одной плате видят общую шину.
  cfg.flags.enable_self_test = 1;
  cfg.flags.enable_loopback = 1;

  if (twai_new_node_onchip(&cfg, &node_) != ESP_OK) {
    ESP_LOGE(kTag, "twai_new_node_onchip failed");
    return false;
  }

  twai_event_callbacks_t cbs = {};
  cbs.on_rx_done = &TwaiCanBus::OnRxDone;
  if (twai_node_register_event_callbacks(node_, &cbs, this) != ESP_OK ||
      twai_node_enable(node_) != ESP_OK) {
    ESP_LOGE(kTag, "twai enable failed");
    return false;
  }
  return true;
}

bool TwaiCanBus::Send(const CanFrame& frame) {
  twai_frame_t tx = {};
  tx.header.id = frame.id;
  tx.header.dlc = frame.dlc;
  tx.buffer = const_cast<uint8_t*>(frame.data);
  tx.buffer_len = frame.dlc > 8 ? 8 : frame.dlc;
  // timeout 0 — неблокирующая постановка в TX-очередь (hot path)
  return twai_node_transmit(node_, &tx, 0) == ESP_OK;
}

std::optional<CanFrame> TwaiCanBus::Poll() {
  TwaiRxItem item = {};
  if (xQueueReceive(rx_queue_, &item, 0) != pdTRUE) {
    return std::nullopt;
  }
  last_rx_ts_ = item.timestamp;
  return item.frame;
}

}  // namespace bench
