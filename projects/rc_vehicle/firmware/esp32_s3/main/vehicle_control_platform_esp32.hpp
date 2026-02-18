#pragma once

struct VehicleControlPlatform;

/** Получить платформенный HAL для ESP32 (очередь команд, задача, PWM, IMU, WebSocket). */
VehicleControlPlatform* VehicleControlPlatformEsp32Get(void);
