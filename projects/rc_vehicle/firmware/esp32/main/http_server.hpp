#pragma once

#include "esp_err.h"

/**
 * Инициализация HTTP сервера для раздачи веб-интерфейса
 * @return ESP_OK при успехе, иначе код ошибки
 */
esp_err_t HttpServerInit(void);
