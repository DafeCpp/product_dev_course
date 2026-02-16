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

/**
 * Запуск ручной калибровки IMU (вызывается из WebSocket-обработчика, Core 0).
 * @param full  true — полная (gyro + accel), false — только гироскоп
 */
void VehicleControlStartCalibration(bool full);

/**
 * Получить строковый статус калибровки IMU.
 * Возвращает: "idle", "collecting", "done", "failed".
 */
const char* VehicleControlGetCalibStatus(void);

