#pragma once

#include <firmware_common/esp32/websocket_server.hpp>
#include <string>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace firmware_common::esp32 {

/**
 * Канал телеметрии поверх WebSocketSendTelem: очередь длины 1
 * (xQueueOverwrite — всегда самый свежий снимок) + низкоприоритетная задача,
 * которая строит JSON из снимка и шлёт его подключенным клиентам.
 *
 * FW-R6: очередь копирует T по значению (memcpy при xQueueOverwrite),
 * продьюсер (control loop) и задача-отправитель не разделяют память — гонка
 * двойной буферизации исключена by-design.
 *
 * FW-RF8: построение JSON (cJSON_PrintUnformatted → sprintf на каждое
 * float-поле, крайне прожорливо по стеку на xtensa newlib) вынесено сюда —
 * низкий приоритет, не 500 Гц control loop. Стек по умолчанию 8192 — не
 * уменьшать: со стеком 3072 был "stack overflow in task ws_telem" (краш в
 * cvt/vfprintf) и reboot-петля на первом же кадре.
 */
template <typename T>
class WsTelemChannel {
 public:
  using JsonBuilder = std::string (*)(const T&);

  /** Идемпотентен: повторный вызов после успешного старта — не-op. */
  esp_err_t Start(JsonBuilder build, const char* task_name = "ws_telem",
                  UBaseType_t prio = 5, uint32_t stack = 8192) {
    if (queue_ != nullptr) {
      return ESP_OK;
    }
    build_ = build;
    queue_ = xQueueCreate(1, sizeof(T));
    if (queue_ == nullptr) {
      return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(&WsTelemChannel::TaskEntry, task_name, stack, this, prio,
                    nullptr) != pdPASS) {
      vQueueDelete(queue_);
      queue_ = nullptr;
      return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
  }

  /**
   * Поставить снимок в очередь (не блокирует вызывающий поток).
   * Детерминированно, без аллокаций — безопасно вызывать из control loop.
   */
  void Enqueue(const T& snap) {
    if (queue_ == nullptr) {
      return;
    }
    xQueueOverwrite(queue_, &snap);
  }

 private:
  static void TaskEntry(void* arg) { static_cast<WsTelemChannel*>(arg)->Run(); }

  void Run() {
    static const char* kTag = "ws_telem_channel";
    uint32_t frames_sent = 0;
    TickType_t last_diag = xTaskGetTickCount();
    for (;;) {
      if (xQueueReceive(queue_, &snap_, portMAX_DELAY) != pdTRUE) {
        continue;
      }
      std::string json = build_(snap_);
      WebSocketSendTelem(json.c_str());
      frames_sent++;

      // Диагностический лог каждые 10 секунд
      TickType_t now = xTaskGetTickCount();
      if ((now - last_diag) >= pdMS_TO_TICKS(10000)) {
        ESP_LOGI(kTag, "%lu frames sent in 10s, clients=%u",
                 (unsigned long)frames_sent,
                 (unsigned)WebSocketGetClientCount());
        frames_sent = 0;
        last_diag = now;
      }
    }
  }

  QueueHandle_t queue_ = nullptr;
  JsonBuilder build_ = nullptr;
  // Член объекта (не локальная переменная задачи): снимок может быть крупным,
  // не хотим раздувать стек задачи; один объект — одна задача, гонок нет.
  T snap_{};
};

}  // namespace firmware_common::esp32
