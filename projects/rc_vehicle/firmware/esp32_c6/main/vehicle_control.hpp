#pragma once

#include "esp_err.h"

/**
 * Инициализация управления машиной (PWM/RC-in/IMU/failsafe) и запуск control loop.
 * @return ESP_OK при успехе
 */
esp_err_t VehicleControlInit(void);

/**
 * Команда от Wi‑Fi (WebSocket).
 * throttle/steering ожидаются в диапазоне [-1..1] (будут дополнительно clamp).
 */
void VehicleControlOnWifiCommand(float throttle, float steering);

