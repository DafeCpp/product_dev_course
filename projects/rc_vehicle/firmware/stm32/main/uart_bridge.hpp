#pragma once

#include <stdint.h>

#include <optional>

#include "protocol.hpp"

// Используем новый API протокола
using rc_vehicle::UartCommand;
using rc_vehicle::protocol::TelemetryData;

/** Команда от ESP32: газ и руль (алиас для совместимости). */
using UartBridgeCommand = rc_vehicle::UartCommand;

int UartBridgeInit(void);
int UartBridgeSendTelem(const rc_vehicle::protocol::TelemetryData &telem_data);
std::optional<UartBridgeCommand> UartBridgeReceiveCommand(void);
bool UartBridgeReceivePing(void);
int UartBridgeSendPong(void);
