#include "valve_emulator.hpp"

#include <cstring>

#include "bench_types.hpp"
#include "esp_timer.h"

namespace bench {

namespace {
constexpr uint8_t kNodeId = 0x20;
constexpr uint32_t kCobNmt = 0x000;
constexpr uint32_t kCobSync = 0x080;
constexpr uint32_t kCobFeedback = 0x180 + kNodeId;   // 0x1A0
constexpr uint32_t kCobSetpoint = 0x200 + kNodeId;   // 0x220
constexpr uint32_t kCobSdoRx = 0x600 + kNodeId;      // 0x620
constexpr uint32_t kCobSdoTx = 0x580 + kNodeId;      // 0x5A0
constexpr uint32_t kCobHeartbeat = 0x700 + kNodeId;  // 0x720

constexpr uint8_t kNmtOperational = 0x05;
constexpr uint8_t kNmtPreOperational = 0x7F;

uint32_t NowMs() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
}  // namespace

void ValveEmulator::TaskEntry(void* arg) {
  static_cast<ValveEmulator*>(arg)->Run();
}

void ValveEmulator::Run() {
  last_step_us_ = static_cast<uint64_t>(esp_timer_get_time());
  TwaiRxItem item = {};
  while (true) {
    if (xQueueReceive(tap_, &item, pdMS_TO_TICKS(2)) == pdTRUE) {
      Handle(item.frame);
    }

    const uint32_t now = NowMs();
    if (now - last_hb_ms_ >= 100) {
      last_hb_ms_ = now;
      CanFrame hb = {};
      hb.id = kCobHeartbeat;
      hb.dlc = 1;
      hb.data[0] = nmt_state_;
      (void)bus_.Send(hb);
    }

    if (fail_at_ms_ != 0 && !failed_ && has_operational_ &&
        now - operational_at_ms_ >= fail_at_ms_) {
      plant_.TriggerFailure();
      failed_ = true;
    }
  }
}

void ValveEmulator::StepPlant() {
  const uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
  float dt = static_cast<float>(now_us - last_step_us_) / 1e6f;
  last_step_us_ = now_us;
  const float cmd = enabled_ ? valve_cmd_ : 0.0f;
  // Дробим редкие крупные dt на шаги ≤2 мс — дискретизация золотника
  // как у 500 Гц контура (совпадает с benchsim/valve_node.py).
  while (dt > 0.0f) {
    const float sub = dt < 0.002f ? dt : 0.002f;
    plant_.Step(cmd, sub);
    dt -= sub;
  }
}

void ValveEmulator::SendFeedback() {
  const auto& s = plant_.GetState();
  CanFrame fb = {};
  fb.id = kCobFeedback;
  fb.dlc = 5;
  const int16_t force_raw = PdoScaling::ForceToRaw(s.force_n);
  const int16_t pos_raw = PdoScaling::PositionToRaw(s.position_mm);
  std::memcpy(&fb.data[0], &force_raw, 2);
  std::memcpy(&fb.data[2], &pos_raw, 2);
  fb.data[4] = 0x07;
  (void)bus_.Send(fb);
}

void ValveEmulator::Handle(const CanFrame& frame) {
  switch (frame.id) {
    case kCobNmt:
      if (frame.dlc >= 2 &&
          (frame.data[1] == kNodeId || frame.data[1] == 0x00)) {
        if (frame.data[0] == 0x01) {
          nmt_state_ = kNmtOperational;
          if (!has_operational_) {
            has_operational_ = true;
            operational_at_ms_ = NowMs();
          }
        } else if (frame.data[0] == 0x02 || frame.data[0] == 0x80) {
          nmt_state_ = kNmtPreOperational;
        }
      }
      break;

    case kCobSetpoint:
      if (nmt_state_ == kNmtOperational && frame.dlc >= 3) {
        StepPlant();  // докатать модель СТАРОЙ командой до применения новой
        int16_t raw = 0;
        std::memcpy(&raw, &frame.data[0], 2);
        valve_cmd_ = PdoScaling::RawToCommand(raw);
        enabled_ = (frame.data[2] & 0x80) != 0;
        // Только в event-режиме feedback идёт на setpoint; в sync —
        // строго по SYNC (иначе прогон был бы одновременно event+sync).
        if (!sync_mode_) {
          SendFeedback();
        }
      }
      break;

    case kCobSync:
      if (nmt_state_ == kNmtOperational && sync_mode_) {
        StepPlant();
        SendFeedback();
      }
      break;

    case kCobSdoRx:
      if (frame.dlc >= 4) {
        // Expedited download 0x1800:02 (тип передачи TPDO) переключает
        // режim feedback — как benchsim/valve_node.py; иначе мастер
        // считает, что sync/event сменился, а эмулятор — нет.
        const uint16_t index = static_cast<uint16_t>(
            frame.data[1] | (static_cast<uint16_t>(frame.data[2]) << 8));
        const uint8_t sub = frame.data[3];
        if (index == 0x1800 && sub == 0x02 && frame.dlc >= 5) {
          sync_mode_ = (frame.data[4] == 0x01);  // 0x01=sync, 0xFE=event
        }
        CanFrame resp = {};
        resp.id = kCobSdoTx;
        resp.dlc = 8;
        resp.data[0] = 0x60;  // download response
        resp.data[1] = frame.data[1];
        resp.data[2] = frame.data[2];
        resp.data[3] = frame.data[3];
        (void)bus_.Send(resp);
      }
      break;

    default:
      break;  // feedback 0x1A0, свой HB, чужие кадры — игнор
  }
}

}  // namespace bench
