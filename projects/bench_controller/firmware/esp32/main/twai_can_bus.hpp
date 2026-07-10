#pragma once

#include "esp_twai.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "i_can_bus.hpp"

namespace bench {

/**
 * @brief ICanBus поверх встроенного TWAI-контроллера ESP32-S3
 *
 * ESP32-S3 имеет один TWAI-контроллер. В режиме self-test + loopback он
 * получает обратно кадры, которые сам отправил, и не требует ACK от
 * второго узла — это позволяет прогнать полный CANopen-контур на одной
 * плате **без внешнего трансивера**: мастер и эмулятор клапана (в той же
 * прошивке) видят общую «виртуальную шину».
 *
 * RX — из ISR-колбэка (новый node-API отдаёт кадры только через
 * `twai_node_receive_from_isr` внутри on_rx_done): колбэк кладёт кадр в
 * очередь мастера и, если задан, в «tap»-очередь эмулятора. Poll()
 * неблокирующе разгребает очередь мастера из тика контура. Аппаратный
 * таймстамп RX (timestamp_resolution_hz) сохраняется — по нему риг
 * считает RTT.
 *
 * API изолирован здесь: при переходе на IDF 5.x (legacy driver/twai.h)
 * меняется только этот файл.
 */
class TwaiCanBus final : public ICanBus {
 public:
  ~TwaiCanBus() override;

  /// tx=rx на одном GPIO в loopback — трансивер не нужен
  [[nodiscard]] bool Init(int gpio_tx, int gpio_rx, uint32_t bitrate);

  [[nodiscard]] bool Send(const CanFrame& frame) override;
  [[nodiscard]] std::optional<CanFrame> Poll() override;

  /// Очередь-«ответвление»: копия каждого RX-кадра для эмулятора клапана
  void SetTap(QueueHandle_t tap) { tap_ = tap; }

  /// Таймстамп последнего кадра, отданного Poll() (тики timebase)
  [[nodiscard]] uint64_t LastRxTimestamp() const { return last_rx_ts_; }

 private:
  static bool OnRxDone(twai_node_handle_t handle,
                       const twai_rx_done_event_data_t* edata, void* ctx);

  twai_node_handle_t node_{nullptr};
  QueueHandle_t rx_queue_{nullptr};
  QueueHandle_t tap_{nullptr};
  uint64_t last_rx_ts_{0};
};

/// Внутренний тип очереди: кадр + аппаратный таймстамп
struct TwaiRxItem {
  CanFrame frame;
  uint64_t timestamp;
};

}  // namespace bench
