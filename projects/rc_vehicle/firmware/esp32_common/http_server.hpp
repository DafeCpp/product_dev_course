#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

/**
 * Инициализация HTTP сервера для раздачи веб-интерфейса
 * @return ESP_OK при успехе, иначе код ошибки
 */
esp_err_t HttpServerInit(void);

/**
 * Получить handle запущенного HTTP-сервера (для регистрации доп. URI, напр. WebSocket).
 * @return httpd_handle_t или NULL если сервер не запущен
 */
httpd_handle_t HttpServerGetHandle(void);
