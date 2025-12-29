#pragma once

#include "esp_err.h"

/**
 * Инициализация Wi-Fi Access Point
 * @return ESP_OK при успехе, иначе код ошибки
 */
esp_err_t WiFiApInit(void);

/**
 * Получить IP адрес AP
 * @param ip_str буфер для строки IP (минимум 16 байт)
 * @return ESP_OK при успехе
 */
esp_err_t WiFiApGetIp(char* ip_str, size_t len);
