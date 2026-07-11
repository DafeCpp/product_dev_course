#pragma once

#include "esp_twai.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
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

  // Постоянные TX-слоты. TWAI node-API кладёт в очередь УКАЗАТЕЛЬ на
  // twai_frame_t и его buffer (не копию), поэтому кадр и payload должны
  // жить до фактической передачи. Send() пишет в слот и продвигает
  // индекс ТОЛЬКО при успешной постановке — тогда:
  //   * слот, израсходованный на неудачной постановке (очередь
  //     переполнена), драйвер НЕ держит → переиспользуется на след.
  //     Send без порчи;
  //   * держатся драйвером ≤ tx_queue_depth (32) слотов, а кольцо 48 >
  //     32 ⇒ слот переиспользуется только через 48 успешных отправок,
  //     когда он давно передан.
  // Выбор+запись+постановка+инкремент атомарны под мьютексом (Send
  // зовут задачи с двух ядер); try-lock без блокировки hot-path — при
  // редкой коллизии кадр дропается (стек трактует как TX overflow).
  static constexpr uint32_t kTxSlots = 48;
  struct TxSlot {
    twai_frame_t frame;
    uint8_t data[8];
  };

  twai_node_handle_t node_{nullptr};
  QueueHandle_t rx_queue_{nullptr};
  QueueHandle_t tap_{nullptr};
  uint64_t last_rx_ts_{0};
  TxSlot tx_slots_[kTxSlots]{};
  uint32_t tx_idx_{0};  ///< под tx_mutex_
  SemaphoreHandle_t tx_mutex_{nullptr};
};

/// Внутренний тип очереди: кадр + аппаратный таймстамп
struct TwaiRxItem {
  CanFrame frame;
  uint64_t timestamp;
};

}  // namespace bench
