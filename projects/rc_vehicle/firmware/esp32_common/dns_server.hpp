#pragma once

#include "esp_err.h"

/**
 * Запуск DNS-сервера для captive portal.
 * Слушает UDP порт 53, отвечает на все запросы IP точки доступа.
 * DHCP должен выдавать 192.168.4.1 как DNS.
 *
 * @param ap_ip IPv4 адрес AP (например 192.168.4.1)
 * @return ESP_OK при успехе
 */
esp_err_t DnsServerStart(uint32_t ap_ip);
