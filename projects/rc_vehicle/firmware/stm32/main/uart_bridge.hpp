#pragma once

#include <stdint.h>

#include <optional>

#include "protocol.hpp"

/** Команда от ESP32: газ и руль. */
struct UartBridgeCommand {
  float throttle{0.f};
  float steering{0.f};
};

int UartBridgeInit(void);
int UartBridgeSendTelem(const TelemetryData &telem_data);
std::optional<UartBridgeCommand> UartBridgeReceiveCommand(void);
bool UartBridgeReceivePing(void);
int UartBridgeSendPong(void);
