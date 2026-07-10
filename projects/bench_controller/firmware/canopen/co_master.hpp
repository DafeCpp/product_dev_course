#pragma once

#include <cstdint>

#include "i_can_bus.hpp"

/* CANopenNode — C99 и typedef анонимных структур: форвард CO_t
 * невозможен, поэтому объект стека храним как void*; инклюды стека
 * живут только в co_master.cpp */
extern "C" {
#include "CO_driver_target.h"  // co_hosted_bus_t (без остального стека)
}

namespace bench {

/**
 * @brief C++-обёртка CANopenNode: мастер одного узла клапана
 *
 * Владеет CO_t и hosted-шиной; наружу — жизненный цикл и пошаговые
 * Process*-вызовы для тика 500 Гц (порядок — см. BenchControlLoop и
 * CanopenValveChannel):
 *
 *   PumpRx → ProcessSyncRpdo → [чтение OD, шаг регулятора, запись OD]
 *   → RequestSetpointTpdo + ProcessTpdo → ProcessMain
 */
class CoMaster {
 public:
  struct Config {
    uint8_t node_id{0x01};        ///< Узел мастера
    uint8_t valve_node_id{0x20};  ///< Узел клапана (HB consumer, NMT, SDO)
    uint16_t first_hb_ms{500};
    uint16_t sdo_timeout_ms{500};
  };

  CoMaster() = default;
  ~CoMaster();
  CoMaster(const CoMaster&) = delete;
  CoMaster& operator=(const CoMaster&) = delete;

  /**
   * @brief Инициализация стека поверх шины
   *
   * CO_new → CO_CANinit → CO_CANopenInit → CO_CANopenInitPDO →
   * normal mode; NMT-мастер сразу шлёт Start remote node клапану.
   */
  [[nodiscard]] bool Init(ICanBus& bus, const Config& config);

  /// Дренаж входящих кадров шины в стек (начало тика)
  void PumpRx();

  /// SYNC-продюсер + обработка RPDO; запоминает syncWas для TPDO
  void ProcessSyncRpdo(uint32_t dt_us);

  /// Запросить отправку TPDO1 (setpoint) — вызывать после записи OD
  void RequestSetpointTpdo();

  /// Обработка TPDO (фактическая отправка)
  void ProcessTpdo(uint32_t dt_us);

  /// NMT/HB/SDO-хозяйство (конец тика)
  void ProcessMain(uint32_t dt_us);

  /// Узел клапана жив: heartbeat свежий и состояние operational
  [[nodiscard]] bool ValveOperational() const;

  /**
   * @brief Блокирующая SDO-запись (expedited, ≤4 байта) узлу клапана
   *
   * Используется ригом вне hot path (настройка transmission type
   * эмулятора). Внутри — цикл PumpRx+CO_SDOclientDownload с таймаутом.
   */
  [[nodiscard]] bool SdoWriteU8(uint16_t index, uint8_t sub, uint8_t value);

  /// Сырой CO_t* (для измерительного рига); внутри — void*
  [[nodiscard]] void* raw() { return co_; }

 private:
  static bool SendShim(void* ctx, uint32_t ident, uint8_t dlc,
                       const uint8_t data[8]);

  void* co_{nullptr};  ///< CO_t* стека
  ICanBus* bus_{nullptr};
  co_hosted_bus_t hosted_bus_{};
  Config config_{};
  bool sync_was_{false};
};

}  // namespace bench
