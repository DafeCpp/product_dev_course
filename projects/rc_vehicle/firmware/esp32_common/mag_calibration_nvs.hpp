#pragma once

#include "esp_err.h"
#include "mag_calibration.hpp"

/**
 * NVS-хранение калибровочных данных магнитометра (hard iron offset).
 *
 * NVS namespace: "mag_calib"
 * Ключ:          "data"
 */
namespace mag_nvs {

/** Сохранить данные калибровки в NVS. */
esp_err_t Save(const MagCalibData& data);

/**
 * Загрузить данные калибровки из NVS.
 * Возвращает ESP_ERR_NOT_FOUND если данных нет или формат устарел.
 */
esp_err_t Load(MagCalibData& data);

/** Удалить данные калибровки из NVS (сброс). */
esp_err_t Erase();

}  // namespace mag_nvs
