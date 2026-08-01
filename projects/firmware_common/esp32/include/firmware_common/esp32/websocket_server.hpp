#pragma once

#include <stdint.h>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_http_server.h"

namespace firmware_common::esp32 {

/** Буфер приёма входящих WS-кадров (входящие команды от браузера). */
inline constexpr size_t kWsRxBufferSize = 1024;
/** Макс. число WS-клиентов, для которых ведётся счётчик неудачных отправок. */
inline constexpr int kWsMaxClients = 4;

/**
 * Колбэк обработки JSON-команд, пришедших по WebSocket (все типы кадров,
 * включая "cmd" — разбор конкретных полей команды остаётся на стороне
 * потребителя). req передаётся для возможности отправить ответ.
 */
using WebSocketJsonHandler = void (*)(const char* type, cJSON* json,
                                      httpd_req_t* req);

/** Установить обработчик JSON-команд (можно вызывать до/после регистрации). */
void WebSocketSetJsonHandler(WebSocketJsonHandler handler);

/**
 * Зарегистрировать WebSocket URI (/ws) на существующем HTTP-сервере.
 * @param server httpd_handle_t от HTTP-сервера
 * @return ESP_OK при успехе
 */
esp_err_t WebSocketRegisterUri(httpd_handle_t server);

/**
 * Отправить телеметрию всем подключенным WebSocket-клиентам.
 * Вызывается из задачи-отправителя телеметрии; не вызывать из control loop.
 * @param telem_json JSON строка с телеметрией
 * @return ESP_OK при успехе
 */
esp_err_t WebSocketSendTelem(const char* telem_json);

/**
 * Опросить httpd и обновить кеш числа подключённых WS-клиентов.
 *
 * LOS-252: дёшево (чтение таблицы сессий httpd, без I/O на клиента) — в
 * отличие от построения телеметрического JSON. Задача телеметрии вызывает
 * это ДО построения кадра, чтобы не платить десятки мс CPU за JSON, который
 * некому отправить; кеш при этом продолжает обновляться, поэтому отправка
 * возобновится сразу после подключения клиента.
 *
 * Вызывать из задачи телеметрии, не из control loop (для него —
 * кешированный WebSocketGetClientCount()).
 *
 * @return количество WS-клиентов; 0 при ошибке опроса
 */
uint8_t WebSocketRefreshAndGetClientCount(void);

/**
 * Получить количество подключенных WebSocket-клиентов (кешированное значение,
 * без обращения к httpd — безопасно из control loop).
 * @return количество клиентов
 */
uint8_t WebSocketGetClientCount(void);

}  // namespace firmware_common::esp32
