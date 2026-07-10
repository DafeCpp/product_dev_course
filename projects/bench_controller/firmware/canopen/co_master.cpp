#include "co_master.hpp"

extern "C" {
#include "CANopen.h"
#include "OD.h"
}

namespace bench {

namespace {
constexpr uint16_t kNmtControl =
    CO_NMT_STARTUP_TO_OPERATIONAL | CO_NMT_ERR_ON_ERR_REG |
    CO_ERR_REG_GENERIC_ERR | CO_ERR_REG_COMMUNICATION;
}  // namespace

namespace {
inline CO_t* AsCo(void* p) { return static_cast<CO_t*>(p); }
}  // namespace

CoMaster::~CoMaster() {
  if (co_ != nullptr) {
    CO_CANsetConfigurationMode(&hosted_bus_);
    CO_delete(AsCo(co_));
  }
}

bool CoMaster::SendShim(void* ctx, uint32_t ident, uint8_t dlc,
                        const uint8_t data[8]) {
  CanFrame frame{};
  frame.id = ident;
  frame.dlc = dlc;
  for (uint8_t i = 0; i < 8; ++i) frame.data[i] = data[i];
  return static_cast<ICanBus*>(ctx)->Send(frame);
}

bool CoMaster::Init(ICanBus& bus, const Config& config) {
  config_ = config;
  bus_ = &bus;
  hosted_bus_.ctx = &bus;
  hosted_bus_.send = &CoMaster::SendShim;

  uint32_t heap_used = 0;
  CO_t* co = CO_new(nullptr, &heap_used);
  co_ = co;
  if (co == nullptr) {
    return false;
  }

  if (CO_CANinit(co, &hosted_bus_, 0 /*bitrate — задаёт владелец шины*/) !=
      CO_ERROR_NO) {
    return false;
  }

  uint32_t err_info = 0;
  CO_ReturnError_t err =
      CO_CANopenInit(co, nullptr, nullptr, OD, nullptr, kNmtControl,
                     config.first_hb_ms, config.sdo_timeout_ms,
                     config.sdo_timeout_ms, false, config.node_id, &err_info);
  if (err != CO_ERROR_NO) {
    return false;
  }
  err = CO_CANopenInitPDO(co, co->em, OD, config.node_id, &err_info);
  if (err != CO_ERROR_NO) {
    return false;
  }

  CO_CANsetNormalMode(co->CANmodule);

  // NMT-мастер: перевести узел клапана в operational (эмулятор
  // python-canopen стартует в pre-operational).
  (void)CO_NMT_sendCommand(co->NMT, CO_NMT_ENTER_OPERATIONAL,
                           config.valve_node_id);
  return true;
}

void CoMaster::PumpRx() {
  // Ограничение на дренаж за тик — защита от шторма на шине.
  for (int i = 0; i < 64; ++i) {
    const std::optional<CanFrame> frame = bus_->Poll();
    if (!frame.has_value()) break;
    co_hosted_receive(AsCo(co_)->CANmodule, frame->id, frame->dlc, frame->data);
  }
}

void CoMaster::ProcessSyncRpdo(uint32_t dt_us) {
  sync_was_ = CO_process_SYNC(AsCo(co_), dt_us, nullptr);
  CO_process_RPDO(AsCo(co_), sync_was_, dt_us, nullptr);
}

void CoMaster::RequestSetpointTpdo() {
  CO_TPDOsendRequest(&AsCo(co_)->TPDO[0]);
}

void CoMaster::ProcessTpdo(uint32_t dt_us) {
  CO_process_TPDO(AsCo(co_), sync_was_, dt_us, nullptr);
}

void CoMaster::ProcessMain(uint32_t dt_us) {
  (void)CO_process(AsCo(co_), false, dt_us, nullptr);
}

bool CoMaster::ValveOperational() const {
  // Consumer настроен единственной записью 0x1016[0] → idx 0.
  const CO_HBconsumer_state_t state =
      CO_HBconsumer_getState(AsCo(co_)->HBcons, 0);
  return state == CO_HBconsumer_ACTIVE;
}

bool CoMaster::SdoWriteU8(uint16_t index, uint8_t sub, uint8_t value) {
  CO_SDOclient_t* sdo = &AsCo(co_)->SDOclient[0];

  if (CO_SDOclient_setup(
          sdo, 0x600U + config_.valve_node_id, 0x580U + config_.valve_node_id,
          config_.valve_node_id) != CO_SDO_RT_ok_communicationEnd) {
    return false;
  }
  if (CO_SDOclientDownloadInitiate(sdo, index, sub, 1, config_.sdo_timeout_ms,
                                   false) != CO_SDO_RT_ok_communicationEnd) {
    return false;
  }
  if (CO_SDOclientDownloadBufWrite(sdo, &value, 1) != 1) {
    return false;
  }

  // Вне hot path: крутим стек шагами по 1 мс до завершения/таймаута.
  constexpr uint32_t kStepUs = 1000;
  const uint32_t max_steps = config_.sdo_timeout_ms * 2;
  for (uint32_t i = 0; i < max_steps; ++i) {
    PumpRx();
    CO_SDO_abortCode_t abort_code = CO_SDO_AB_NONE;
    const CO_SDO_return_t ret = CO_SDOclientDownload(
        sdo, kStepUs, false, false, &abort_code, nullptr, nullptr);
    if (ret == CO_SDO_RT_ok_communicationEnd) {
      return true;
    }
    if (ret < 0 || abort_code != CO_SDO_AB_NONE) {
      return false;
    }
  }
  return false;
}

}  // namespace bench
