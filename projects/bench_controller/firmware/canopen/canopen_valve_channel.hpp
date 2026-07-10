#pragma once

#include <cstdint>

#include "bench_types.hpp"
#include "co_master.hpp"
#include "valve_channel.hpp"

namespace bench {

/**
 * @brief IValveChannel поверх CANopen (OD мастера)
 *
 * Шов между портируемым ядром и стеком:
 * - PreTick: дренаж шины + SYNC-продюсер + обработка RPDO (feedback
 *   ложится в OD 0x2120..0x2122);
 * - WriteSetpoint: физические величины → int16-кванты → OD 0x2110/0x2111;
 * - PostTick: запрос и отправка TPDO1 + NMT/HB-хозяйство;
 * - свежесть feedback — по счётчику применённых RPDO (OD-extension
 *   на 0x2122, последнем поле маппинга).
 */
class CanopenValveChannel final : public IValveChannel {
 public:
  /// Подключить к мастеру и поставить OD-extension на 0x2122.
  /// ВАЖНО: вызывать ДО CoMaster::Init — PDO-инициализация стека
  /// выбирает способ доступа к OD в момент CO_CANopenInitPDO, и
  /// extension, установленный позже, RPDO не видит.
  void Attach(CoMaster& master);

  void PreTick(uint32_t dt_us) override;
  void WriteSetpoint(const ValveSetpoint& sp) override;
  [[nodiscard]] ValveFeedback ReadFeedback() override;
  void PostTick(uint32_t dt_us) override;
  [[nodiscard]] bool IsOperational() const override;

  /// Счётчик принятых feedback-PDO (для измерений рига)
  [[nodiscard]] uint32_t FeedbackCount() const;

 private:
  CoMaster* master_{nullptr};
  uint32_t last_seen_count_{0};
  uint32_t age_ticks_{0};
  bool setpoint_pending_{false};
};

}  // namespace bench
