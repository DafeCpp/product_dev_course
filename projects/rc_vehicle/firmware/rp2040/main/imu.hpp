#pragma once

#include <stdbool.h>
#include <stdint.h>

// Структура для данных IMU
struct ImuData {
  float ax, ay, az; // Акселерометр (g)
  float gx, gy, gz; // Гироскоп (dps)
};

/**
 * Инициализация IMU (MPU-6050)
 * @return 0 при успехе, -1 при ошибке
 */
int ImuInit(void);

/**
 * Чтение данных с IMU
 * @param data указатель на структуру для данных
 * @return 0 при успехе, -1 при ошибке
 */
int ImuRead(ImuData *data);

/**
 * Конвертация данных IMU в формат для телеметрии (int16)
 * @param data данные IMU
 * @param ax, ay, az указатели для акселерометра (mg)
 * @param gx, gy, gz указатели для гироскопа (mdps)
 */
void ImuConvertToTelem(const ImuData *data, int16_t *ax, int16_t *ay,
                       int16_t *az, int16_t *gx, int16_t *gy, int16_t *gz);
