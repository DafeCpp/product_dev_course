#include "canopen_valve_channel.hpp"

extern "C" {
#include "CANopen.h"
#include "OD.h"
}

namespace bench {

namespace {

// Счётчик применённых feedback-PDO. RPDO пишет замапленные переменные
// по порядку маппинга; extension стоит на 0x2122 (status — последнее
// поле), так что инкремент означает «весь PDO применён».
uint32_t g_feedback_rx_count = 0;
OD_extension_t g_status_ext{};

ODR_t StatusWriteHook(OD_stream_t* stream, const void* buf, OD_size_t count,
                      OD_size_t* countWritten) {
  const ODR_t ret = OD_writeOriginal(stream, buf, count, countWritten);
  if (ret == ODR_OK) {
    ++g_feedback_rx_count;
  }
  return ret;
}

}  // namespace

void CanopenValveChannel::Attach(CoMaster& master) {
  master_ = &master;
  g_feedback_rx_count = 0;
  last_seen_count_ = 0;
  age_ticks_ = 0;

  g_status_ext.object = nullptr;
  g_status_ext.read = OD_readOriginal;
  g_status_ext.write = StatusWriteHook;
  OD_extension_init(OD_ENTRY_H2122_valveStatus, &g_status_ext);
}

void CanopenValveChannel::PreTick(uint32_t dt_us) {
  master_->PumpRx();
  master_->ProcessSyncRpdo(dt_us);
}

void CanopenValveChannel::WriteSetpoint(const ValveSetpoint& sp) {
  OD_RAM.x2110_valveSetpoint = PdoScaling::CommandToRaw(sp.value);
  uint8_t control_word = static_cast<uint8_t>(sp.mode) & 0x03U;
  if (sp.enable) {
    control_word |= 0x80U;
  }
  OD_RAM.x2111_controlWord = control_word;
  setpoint_pending_ = true;
}

ValveFeedback CanopenValveChannel::ReadFeedback() {
  ValveFeedback fb{};
  fb.force_n = PdoScaling::RawToForce(OD_RAM.x2120_actualForce);
  fb.position_mm = PdoScaling::RawToPosition(OD_RAM.x2121_actualPosition);
  fb.status = OD_RAM.x2122_valveStatus;

  const uint32_t count = g_feedback_rx_count;
  fb.fresh = count != last_seen_count_;
  age_ticks_ = fb.fresh ? 0 : age_ticks_ + 1;
  fb.age_ticks = age_ticks_;
  last_seen_count_ = count;
  return fb;
}

void CanopenValveChannel::PostTick(uint32_t dt_us) {
  if (setpoint_pending_) {
    master_->RequestSetpointTpdo();
    setpoint_pending_ = false;
  }
  master_->ProcessTpdo(dt_us);
  master_->ProcessMain(dt_us);
}

bool CanopenValveChannel::IsOperational() const {
  return master_ != nullptr && master_->ValveOperational();
}

uint32_t CanopenValveChannel::FeedbackCount() const {
  return g_feedback_rx_count;
}

}  // namespace bench
