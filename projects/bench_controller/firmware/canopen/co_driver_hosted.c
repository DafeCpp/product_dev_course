/*
 * co_driver_hosted.c — единый CO_driver bench_controller поверх
 * фреймового шима co_hosted_bus_t (SocketCAN / FakeCanBus / TWAI).
 *
 * Отправка синхронная: CO_CANsend сразу зовёт bus->send; неудача
 * учитывается как TX overflow (для vcan/фейка практически недостижимо;
 * для реальной шины ESP32 ретраи — задача PR-C). Приём — polled:
 * владелец шины дренирует кадры и передаёт их в co_hosted_receive,
 * которая повторяет матчинг rxArray из шаблона CO_CANinterrupt.
 *
 * По образцу example/CO_driver_blank.c апстрима (Apache-2.0).
 */

#include "301/CO_driver.h"

void CO_CANsetConfigurationMode(void* CANptr) { (void)CANptr; }

void CO_CANsetNormalMode(CO_CANmodule_t* CANmodule) {
  CANmodule->CANnormal = true;
}

CO_ReturnError_t CO_CANmodule_init(CO_CANmodule_t* CANmodule, void* CANptr,
                                   CO_CANrx_t rxArray[], uint16_t rxSize,
                                   CO_CANtx_t txArray[], uint16_t txSize,
                                   uint16_t CANbitRate) {
  uint16_t i;
  (void)CANbitRate; /* скорость задаёт владелец шины */

  if (CANmodule == NULL || rxArray == NULL || txArray == NULL ||
      CANptr == NULL) {
    return CO_ERROR_ILLEGAL_ARGUMENT;
  }

  CANmodule->CANptr = CANptr;
  CANmodule->rxArray = rxArray;
  CANmodule->rxSize = rxSize;
  CANmodule->txArray = txArray;
  CANmodule->txSize = txSize;
  CANmodule->CANerrorStatus = 0;
  CANmodule->CANnormal = false;
  CANmodule->useCANrxFilters = false; /* линейный поиск по rxArray */
  CANmodule->bufferInhibitFlag = false;
  CANmodule->firstCANtxMessage = true;
  CANmodule->CANtxCount = 0U;
  CANmodule->errOld = 0U;

  for (i = 0U; i < rxSize; i++) {
    rxArray[i].ident = 0U;
    rxArray[i].mask = 0xFFFFU;
    rxArray[i].object = NULL;
    rxArray[i].CANrx_callback = NULL;
  }
  for (i = 0U; i < txSize; i++) {
    txArray[i].bufferFull = false;
  }

  return CO_ERROR_NO;
}

void CO_CANmodule_disable(CO_CANmodule_t* CANmodule) {
  if (CANmodule != NULL) {
    CANmodule->CANnormal = false;
  }
}

CO_ReturnError_t CO_CANrxBufferInit(CO_CANmodule_t* CANmodule, uint16_t index,
                                    uint16_t ident, uint16_t mask, bool_t rtr,
                                    void* object,
                                    void (*CANrx_callback)(void* object,
                                                           void* message)) {
  if ((CANmodule == NULL) || (object == NULL) || (CANrx_callback == NULL) ||
      (index >= CANmodule->rxSize)) {
    return CO_ERROR_ILLEGAL_ARGUMENT;
  }

  CO_CANrx_t* buffer = &CANmodule->rxArray[index];
  buffer->object = object;
  buffer->CANrx_callback = CANrx_callback;
  buffer->ident = ident & 0x07FFU;
  if (rtr) {
    buffer->ident |= 0x0800U;
  }
  buffer->mask = (mask & 0x07FFU) | 0x0800U;

  return CO_ERROR_NO;
}

CO_CANtx_t* CO_CANtxBufferInit(CO_CANmodule_t* CANmodule, uint16_t index,
                               uint16_t ident, bool_t rtr, uint8_t noOfBytes,
                               bool_t syncFlag) {
  CO_CANtx_t* buffer = NULL;

  /* RTR в этом тракте не используется */
  if ((CANmodule != NULL) && (index < CANmodule->txSize) && !rtr) {
    buffer = &CANmodule->txArray[index];
    buffer->ident = (uint32_t)ident & 0x07FFU;
    buffer->DLC = (uint8_t)(noOfBytes & 0x0FU);
    buffer->bufferFull = false;
    buffer->syncFlag = syncFlag;
  }

  return buffer;
}

CO_ReturnError_t CO_CANsend(CO_CANmodule_t* CANmodule, CO_CANtx_t* buffer) {
  co_hosted_bus_t* bus = (co_hosted_bus_t*)CANmodule->CANptr;

  if (!bus->send(bus->ctx, buffer->ident, buffer->DLC, buffer->data)) {
    CANmodule->CANerrorStatus |= CO_CAN_ERRTX_OVERFLOW;
    return CO_ERROR_TX_OVERFLOW;
  }
  CANmodule->firstCANtxMessage = false;
  return CO_ERROR_NO;
}

void CO_CANclearPendingSyncPDOs(CO_CANmodule_t* CANmodule) {
  /* Отправка синхронная — отложенных кадров в драйвере нет */
  (void)CANmodule;
}

void CO_CANmodule_process(CO_CANmodule_t* CANmodule) {
  /* Ошибок шины hosted-шим не сообщает; статус остаётся как есть.
   * Для реальной шины (TWAI) счётчики ошибок — задача PR-C. */
  (void)CANmodule;
}

void co_hosted_receive(CO_CANmodule_t* CANmodule, uint32_t ident, uint8_t dlc,
                       const uint8_t data[8]) {
  CO_CANrxMsg_t msg;
  CO_CANrx_t* buffer;
  uint16_t index;
  uint8_t i;

  if (CANmodule == NULL || !CANmodule->CANnormal) {
    return;
  }

  msg.ident = ident & 0x07FFU;
  msg.DLC = dlc > 8U ? 8U : dlc;
  for (i = 0; i < msg.DLC; i++) {
    msg.data[i] = data[i];
  }

  buffer = &CANmodule->rxArray[0];
  for (index = CANmodule->rxSize; index > 0U; index--) {
    if (((msg.ident ^ buffer->ident) & buffer->mask) == 0U) {
      if (buffer->CANrx_callback != NULL) {
        buffer->CANrx_callback(buffer->object, (void*)&msg);
      }
      break;
    }
    buffer++;
  }
}
