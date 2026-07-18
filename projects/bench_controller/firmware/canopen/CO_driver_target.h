/*
 * CO_driver_target.h — таргет-хедер CANopenNode для bench_controller.
 *
 * Единый hosted-драйвер: стек ходит в шину через фреймовый шим
 * (co_hosted_bus_t), реализации которого — SocketCAN (Linux),
 * FakeCanBus (тесты), TWAI (ESP32, PR-C). Контур однопоточный
 * (все CO_* вызовы из тика), поэтому lock-макросы пустые.
 *
 * По образцу example/CO_driver_target.h апстрима (Apache-2.0).
 */

#ifndef CO_DRIVER_TARGET_H
#define CO_DRIVER_TARGET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Конфигурация стека (переопределяет дефолты CO_config.h) ---
 * Мастер одного клапана: NMT-master, HB-consumer, SYNC-producer,
 * PDO, SDO client+server, EM. LSS/TIME/LEDS/STORAGE/GFC/SRDO — выкл. */
#define CO_CONFIG_NMT (CO_CONFIG_NMT_MASTER)
#define CO_CONFIG_HB_CONS \
  (CO_CONFIG_HB_CONS_ENABLE | CO_CONFIG_HB_CONS_QUERY_FUNCT)
#define CO_CONFIG_EM (CO_CONFIG_EM_PRODUCER | CO_CONFIG_EM_HISTORY)
/* SDO-сервер обязателен по CiA301 — конфиг по умолчанию */
#define CO_CONFIG_SDO_CLI \
  (CO_CONFIG_SDO_CLI_ENABLE | CO_CONFIG_SDO_CLI_SEGMENTED)
#define CO_CONFIG_SYNC (CO_CONFIG_SYNC_ENABLE | CO_CONFIG_SYNC_PRODUCER)
/* OD_IO_ACCESS: PDO ходит в OD через read/write (уважает
 * OD-extensions — на этом построена свежесть feedback) */
#define CO_CONFIG_PDO \
  (CO_CONFIG_RPDO_ENABLE | CO_CONFIG_TPDO_ENABLE | CO_CONFIG_PDO_OD_IO_ACCESS)
#define CO_CONFIG_TIME (0)
#define CO_CONFIG_LEDS (0)
#define CO_CONFIG_LSS (0)
#define CO_CONFIG_GFC (0)
#define CO_CONFIG_SRDO (0)
#define CO_CONFIG_STORAGE (0)
#define CO_CONFIG_FIFO (CO_CONFIG_FIFO_ENABLE) /* нужен SDO-клиенту */
#define CO_CONFIG_GTW (0)

/* --- Базовые типы --- */
#define CO_LITTLE_ENDIAN
#define CO_SWAP_16(x) x
#define CO_SWAP_32(x) x
#define CO_SWAP_64(x) x
typedef uint_fast8_t bool_t;
typedef float float32_t;
typedef double float64_t;

/* --- Принятый кадр --- */
typedef struct {
  uint32_t ident;
  uint8_t DLC;
  uint8_t data[8];
} CO_CANrxMsg_t;

#define CO_CANrxMsg_readIdent(msg) ((uint16_t)(((CO_CANrxMsg_t*)(msg))->ident))
#define CO_CANrxMsg_readDLC(msg) ((uint8_t)(((CO_CANrxMsg_t*)(msg))->DLC))
#define CO_CANrxMsg_readData(msg) \
  ((const uint8_t*)(((CO_CANrxMsg_t*)(msg))->data))

/* Объект приёмного фильтра */
typedef struct {
  uint16_t ident;
  uint16_t mask;
  void* object;
  void (*CANrx_callback)(void* object, void* message);
} CO_CANrx_t;

/* Объект передачи */
typedef struct {
  uint32_t ident; /* чистый 11-битный идентификатор */
  uint8_t DLC;
  uint8_t data[8];
  volatile bool_t bufferFull;
  volatile bool_t syncFlag;
} CO_CANtx_t;

/* CAN-модуль */
typedef struct {
  void* CANptr; /* co_hosted_bus_t* */
  CO_CANrx_t* rxArray;
  uint16_t rxSize;
  CO_CANtx_t* txArray;
  uint16_t txSize;
  uint16_t CANerrorStatus;
  volatile bool_t CANnormal;
  volatile bool_t useCANrxFilters;
  volatile bool_t bufferInhibitFlag;
  volatile bool_t firstCANtxMessage;
  volatile uint16_t CANtxCount;
  uint32_t errOld;
} CO_CANmodule_t;

/* Хранилище не используется (CO_CONFIG_STORAGE = 0), но тип нужен
 * заголовкам */
typedef struct {
  void* addr;
  size_t len;
  uint8_t subIndexOD;
  uint8_t attr;
} CO_storage_entry_t;

/* Однопоточный контур — критические секции не нужны */
#define CO_LOCK_CAN_SEND(CAN_MODULE)
#define CO_UNLOCK_CAN_SEND(CAN_MODULE)
#define CO_LOCK_EMCY(CAN_MODULE)
#define CO_UNLOCK_EMCY(CAN_MODULE)
#define CO_LOCK_OD(CAN_MODULE)
#define CO_UNLOCK_OD(CAN_MODULE)

#define CO_MemoryBarrier()
#define CO_FLAG_READ(rxNew) ((rxNew) != NULL)
#define CO_FLAG_SET(rxNew) \
  {                        \
    CO_MemoryBarrier();    \
    rxNew = (void*)1L;     \
  }
#define CO_FLAG_CLEAR(rxNew) \
  {                          \
    CO_MemoryBarrier();      \
    rxNew = NULL;            \
  }

/* --- Шим шины для hosted-драйвера ---
 * CANptr при CO_CANinit должен указывать на этот struct; send
 * возвращает false при невозможности отправить (учитывается как
 * TX overflow). */
typedef struct co_hosted_bus {
  void* ctx;
  bool (*send)(void* ctx, uint32_t ident, uint8_t dlc, const uint8_t data[8]);
} co_hosted_bus_t;

/* Входная точка приёма: вызывается владельцем шины при дренаже
 * (сопоставление с rxArray + вызов callback — внутри). */
void co_hosted_receive(CO_CANmodule_t* CANmodule, uint32_t ident, uint8_t dlc,
                       const uint8_t data[8]);

#ifdef __cplusplus
}
#endif

#endif /* CO_DRIVER_TARGET_H */
