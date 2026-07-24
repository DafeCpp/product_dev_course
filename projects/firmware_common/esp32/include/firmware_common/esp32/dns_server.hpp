#pragma once

#include <cstdint>

#include "esp_err.h"

namespace firmware_common::esp32 {

/**
 * Запуск DNS-сервера для captive portal.
 * Слушает UDP порт 53, отвечает на все запросы IP точки доступа.
 * DHCP должен выдавать 192.168.4.1 как DNS.
 *
 * @param ap_ip IPv4 адрес AP (например 192.168.4.1)
 * @return ESP_OK при успехе
 */
esp_err_t DnsServerStart(uint32_t ap_ip);

/**
 * Остановить DNS-сервер и освободить UDP-порт 53. Не-op, если сервер не
 * запущен. Нужен при runtime-выключении радио (голова, LOS-180): порт 53
 * должен освобождаться вместе с Wi-Fi, а не оставаться занятым.
 *
 * Ждёт фактического завершения задачи (не дольше ~1с) — иначе следующий
 * DnsServerStart() мог бы решить, что сервер ещё работает, и не создать
 * замену. Если задача не успела выйти за это время — ESP_ERR_TIMEOUT
 * (сама задача всё равно продолжит останавливаться и в итоге завершится).
 * @return ESP_OK при успехе, ESP_ERR_TIMEOUT при таймауте ожидания
 */
esp_err_t DnsServerStop(void);

}  // namespace firmware_common::esp32
