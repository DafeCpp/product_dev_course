#pragma once

#include <stdint.h>

#include <optional>

#include "protocol.hpp"

// Используем новый API протокола
using rc_vehicle::UartCommand;
using rc_vehicle::protocol::TelemetryData;

/**
 * Инициализация UART моста к ESP32
 * @return 0 при успехе, -1 при ошибке
 */
int UartBridgeInit(void);

/**
 * Отправка телеметрии на ESP32
 * @param telem_data данные телеметрии
 * @return 0 при успехе, -1 при ошибке
 */
int UartBridgeSendTelem(const rc_vehicle::protocol::TelemetryData &telem_data);

/** Команда от ESP32: газ и руль (алиас для совместимости). */
using UartBridgeCommand = rc_vehicle::UartCommand;

/**
 * Попытка принять команду от ESP32
 * @return команда (throttle, steering) или std::nullopt, если команды нет
 */
std::optional<UartBridgeCommand> UartBridgeReceiveCommand();

/** Принять PING от ESP32 (нужно ответить UartBridgeSendPong). */
bool UartBridgeReceivePing(void);
/** Отправить PONG в ответ на PING. */
int UartBridgeSendPong(void);

/**
 * Отправить текстовое LOG-сообщение на ESP32 (для удалённой отладки).
 * @param msg текст сообщения
 * @param len длина сообщения (до 200 байт)
 * @return 0 при успехе, -1 при ошибке
 */
int UartBridgeSendLog(const char *msg, size_t len);
