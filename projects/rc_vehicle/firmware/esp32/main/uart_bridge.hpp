#pragma once

#include <optional>

#include "esp_err.h"
#include "protocol.hpp"

// Используем новый API протокола
using rc_vehicle::protocol::TelemetryData;

/**
 * Инициализация UART моста к RP2040
 * @return ESP_OK при успехе, иначе код ошибки
 */
esp_err_t UartBridgeInit(void);

/**
 * Отправить команду управления на RP2040
 * @param throttle значение газа [-1.0..1.0]
 * @param steering значение руля [-1.0..1.0]
 * @return ESP_OK при успехе
 */
esp_err_t UartBridgeSendCommand(float throttle, float steering);

/**
 * Получить телеметрию от RP2040 (неблокирующий вызов)
 * @return данные телеметрии или std::nullopt, если нет данных
 */
std::optional<rc_vehicle::protocol::TelemetryData> UartBridgeReceiveTelem(void);

/**
 * Отправить PING на MCU (Pico/STM32)
 * @return ESP_OK при успехе
 */
esp_err_t UartBridgeSendPing(void);

/**
 * Получить PONG от MCU (неблокирующий)
 * @return ESP_OK если PONG получен, ESP_ERR_NOT_FOUND если нет
 */
esp_err_t UartBridgeReceivePong(void);

/**
 * Связь с MCU по PONG (PING был отправлен, PONG получен недавно)
 * @return true если PONG получен в последние ~1.5 с
 */
bool UartBridgeIsMcuConnected(void);

/**
 * Получить LOG-сообщение от MCU (неблокирующий вызов).
 * @param buf буфер для текста
 * @param max_len размер буфера
 * @return длина сообщения (>0) или 0 если нет данных
 */
int UartBridgeReceiveLog(char *buf, size_t max_len);
