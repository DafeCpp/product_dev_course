#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

/**
 * Зарегистрировать RC-специфичные HTTP-маршруты (веб-страница управления,
 * бинарный лог телеметрии, crash-лог) на уже запущенном общем HTTP-сервере
 * (firmware_common::esp32::HttpServerInit).
 * @param server httpd_handle_t от общего HTTP-сервера
 * @return ESP_OK при успехе
 */
esp_err_t RcHttpRegisterRoutes(httpd_handle_t server);
