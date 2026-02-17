#pragma once

#include <stdint.h>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_http_server.h"

/** Колбэк обработки команды управления из WebSocket. */
using WebSocketCommandHandler = void (*)(float throttle, float steering);

/**
 * Колбэк для обработки произвольных JSON-команд через WebSocket.
 * Вызывается для всех типов, кроме "cmd" (которые обрабатываются
 * WebSocketCommandHandler). req передаётся для возможности отправить ответ.
 */
using WebSocketJsonHandler = void (*)(const char* type, cJSON* json,
                                      httpd_req_t* req);

/** Установить обработчик команд (можно вызывать до/после регистрации). */
void WebSocketSetCommandHandler(WebSocketCommandHandler handler);

/** Установить обработчик произвольных JSON-команд. */
void WebSocketSetJsonHandler(WebSocketJsonHandler handler);

/**
 * Зарегистрировать WebSocket URI (/ws) на существующем HTTP-сервере.
 * @param server httpd_handle_t от HTTP-сервера (порт 80)
 * @return ESP_OK при успехе
 */
esp_err_t WebSocketRegisterUri(httpd_handle_t server);

/**
 * Отправить телеметрию всем подключенным WebSocket-клиентам.
 * @param telem_json JSON строка с телеметрией
 * @return ESP_OK при успехе
 */
esp_err_t WebSocketSendTelem(const char* telem_json);

/**
 * Получить количество подключенных WebSocket-клиентов.
 * @return количество клиентов
 */
uint8_t WebSocketGetClientCount(void);
