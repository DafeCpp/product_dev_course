#pragma once

#include <stdint.h>

#include "esp_err.h"

/**
 * Инициализация WebSocket сервера
 * @return ESP_OK при успехе, иначе код ошибки
 */
esp_err_t WebSocketServerInit(void);

/**
 * Отправить телеметрию всем подключенным клиентам
 * @param telem_json JSON строка с телеметрией
 * @return ESP_OK при успехе
 */
esp_err_t WebSocketSendTelem(const char* telem_json);

/**
 * Получить количество подключенных клиентов
 * @return количество клиентов
 */
uint8_t WebSocketGetClientCount(void);
