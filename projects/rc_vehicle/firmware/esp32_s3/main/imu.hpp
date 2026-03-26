#pragma once

#include <stdint.h>

#include "imu_sensor.hpp"

// C-API для main (реализация через IImuSensor с runtime-детектом MPU6050/LSM6DS3)

/** Инициализация IMU (автодетект: LSM6DS3 → MPU6050). 0 — успех, -1 — ошибка. */
int ImuInit(void);

/** Чтение данных с IMU. 0 — успех, -1 — ошибка. */
int ImuRead(ImuData& data);

/** Конвертация данных IMU в формат телеметрии (mg, mdps → int16). */
void ImuConvertToTelem(const ImuData& data, int16_t& ax, int16_t& ay, int16_t& az,
                       int16_t& gx, int16_t& gy, int16_t& gz);

/** Для отладки: последнее WHO_AM_I при инициализации (-1 = не читали). */
int ImuGetLastWhoAmI(void);

/** Имя активного датчика: "LSM6DS3", "LSM6DSL", "MPU6050", "MPU6500" или "none". */
const char* ImuGetSensorName(void);
