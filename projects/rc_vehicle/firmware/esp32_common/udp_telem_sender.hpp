#pragma once

#include <cstdint>

#include "esp_err.h"
#include "telemetry_log.hpp"

/**
 * @brief Инициализировать модуль UDP-стриминга телеметрии
 *
 * Создает:
 * - FreeRTOS очередь для TelemetryLogFrame
 * - UDP control socket на порту 5556
 * - Задачу udp_ctrl_task (прием команд START/STOP/STATUS/PING)
 * - Задачу udp_sender_task (отправка телеметрии из очереди)
 *
 * Загружает последний target из NVS (но не начинает стриминг).
 *
 * @return ESP_OK при успехе
 */
esp_err_t UdpTelemInit();

/**
 * @brief Поставить кадр телеметрии в очередь на отправку
 *
 * Вызывается из control loop. Если стриминг не активен — no-op.
 * Если очередь полна — кадр отбрасывается, счетчик dropped++.
 *
 * @param frame Кадр телеметрии
 */
void UdpTelemEnqueue(const TelemetryLogFrame& frame);

/**
 * @brief Запустить стриминг
 *
 * @param ip IPv4 адрес получателя (строка "x.x.x.x")
 * @param port UDP порт получателя
 * @param hz Частота отправки (10, 20, 50, 100)
 * @return true при успехе
 */
bool UdpTelemStart(const char* ip, uint16_t port, uint8_t hz);

/**
 * @brief Остановить стриминг
 */
void UdpTelemStop();

/**
 * @brief Проверить, активен ли стриминг
 */
bool UdpTelemIsStreaming();

/**
 * @brief Получить текущий sequence number
 */
uint32_t UdpTelemGetSeq();

/**
 * @brief Получить количество отброшенных кадров (переполнение очереди)
 */
uint32_t UdpTelemGetDropped();

/**
 * @brief Получить текущий target IP (строка "x.x.x.x") или ""
 */
const char* UdpTelemGetTargetIp();

/**
 * @brief Получить текущий target port
 */
uint16_t UdpTelemGetTargetPort();

/**
 * @brief Получить текущую частоту
 */
uint8_t UdpTelemGetHz();
