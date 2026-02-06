#pragma once

#include <stdint.h>

#include "mpu6050_spi.hpp"

// ImuData и C-API для main (реализация через Mpu6050Spi + SpiStm32)

/** Инициализация IMU (MPU-6050 по SPI). 0 — успех, -1 — ошибка. */
int ImuInit();

/** Чтение данных с IMU. 0 — успех, -1 — ошибка. */
int ImuRead(ImuData &data);

/** Конвертация данных IMU в формат телеметрии (mg, mdps → int16). */
void ImuConvertToTelem(const ImuData &data, int16_t &ax, int16_t &ay,
                       int16_t &az, int16_t &gx, int16_t &gy, int16_t &gz);
