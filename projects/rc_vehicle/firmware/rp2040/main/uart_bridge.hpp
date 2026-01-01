#pragma once

#include <stdint.h>

/**
 * Инициализация UART моста к ESP32
 * @return 0 при успехе, -1 при ошибке
 */
int UartBridgeInit(void);

/**
 * Отправка телеметрии на ESP32
 * @param telem_data указатель на данные телеметрии
 * @return 0 при успехе, -1 при ошибке
 */
int UartBridgeSendTelem(const void *telem_data);

/**
 * Попытка принять команду от ESP32
 * @param throttle указатель для значения газа (будет заполнен)
 * @param steering указатель для значения руля (будет заполнен)
 * @return true если команда получена, false если нет
 */
bool UartBridgeReceiveCommand(float *throttle, float *steering);
