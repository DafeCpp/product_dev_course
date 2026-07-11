#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "hydraulic_plant_model.hpp"
#include "twai_can_bus.hpp"

namespace bench {

/**
 * @brief On-device эмулятор узла клапана Atos (node 0x20)
 *
 * Аналог benchsim/valve_node.py, но на C++ и на реальной TWAI-шине
 * (loopback). Задача на ядре 0 читает tap-очередь TwaiCanBus, реагирует
 * на сырые кадры и шлёт feedback через ту же шину:
 * - NMT (0x000): Start/Stop для узла 0x20;
 * - setpoint RPDO (0x220): int16 команда + control word → шаг модели;
 *   в event-режиме сразу шлёт feedback;
 * - SYNC (0x080): в sync-режиме шаг модели + feedback;
 * - SDO (0x620): expedited download подтверждается (заглушка);
 * - heartbeat 0x720 каждые 100 мс.
 *
 * Собственные кадры (feedback 0x1A0, свой HB) и кадры мастера, которые
 * не адресованы клапану, игнорируются.
 */
class ValveEmulator {
 public:
  ValveEmulator(TwaiCanBus& bus, QueueHandle_t tap, bool sync_mode)
      : bus_(bus), tap_(tap), sync_mode_(sync_mode) {}

  /// Точка входа задачи FreeRTOS (arg = ValveEmulator*)
  static void TaskEntry(void* arg);

  /// Разрушение образца в момент now_ms (0 = не срабатывает)
  void SetFailAtMs(uint32_t now_ms) { fail_at_ms_ = now_ms; }

 private:
  void Run();
  void Handle(const CanFrame& frame);
  void StepPlant();
  void SendFeedback();

  TwaiCanBus& bus_;
  QueueHandle_t tap_;
  bool sync_mode_;

  HydraulicPlantModel plant_{};
  uint8_t nmt_state_{0x7F};  // pre-operational
  float valve_cmd_{0.0f};
  bool enabled_{false};
  uint64_t last_step_us_{0};
  uint32_t last_hb_ms_{0};
  uint32_t operational_at_ms_{0};
  bool has_operational_{false};
  uint32_t fail_at_ms_{0};
  bool failed_{false};
};

}  // namespace bench
