#pragma once

#include "mag_sensor.hpp"

// C-API для main (реализация через IMagSensor, Mmc5983Spi)

/** Инициализация магнитометра. 0 — успех, -1 — ошибка. */
int MagInit(void);

/** Чтение данных с магнитометра. 0 — успех, -1 — ошибка. */
int MagRead(MagData& data);

/** Последнее прочитанное Product ID (-1 = не читали). */
int MagGetLastProductId(void);

/** Имя активного датчика: "MMC5983MA" или "none". */
const char* MagGetSensorName(void);
