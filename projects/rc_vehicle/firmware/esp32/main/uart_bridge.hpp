#pragma once

#include "esp_err.h"

/**
 * Инициализация UART моста к RP2040
 * @return ESP_OK при успехе, иначе код ошибки
 */
esp_err_t UartBridgeInit(void);

/**
 * Отправить команду управления на RP2040
 * @param throttle значение газа [-1.0..1.0]
 * @param steering значение руля [-1.0..1.0]
 * @return ESP_OK при успехе
 */
esp_err_t UartBridgeSendCommand(float throttle, float steering);

/**
 * Получить телеметрию от RP2040 (неблокирующий вызов)
 * @param telem_data указатель на структуру для данных (будет заполнена при
 * наличии)
 * @return ESP_OK если данные получены, ESP_ERR_NOT_FOUND если нет данных
 */
esp_err_t UartBridgeReceiveTelem(void* telem_data);
