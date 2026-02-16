#pragma once

#include "esp_err.h"
#include "imu_calibration.hpp"

/**
 * NVS-хранение калибровочных данных IMU.
 *
 * Данные сохраняются как blob с версионным заголовком — при обновлении формата
 * старые данные автоматически отбрасываются.
 *
 * NVS namespace: "imu_calib"
 * Ключ:          "data"
 */
namespace imu_nvs {

/** Сохранить калибровочные данные в NVS. */
esp_err_t Save(const ImuCalibData& data);

/** Загрузить калибровочные данные из NVS.
 *  Возвращает ESP_ERR_NOT_FOUND если данных нет или формат устарел. */
esp_err_t Load(ImuCalibData& data);

/** Удалить калибровочные данные из NVS (сброс). */
esp_err_t Erase();

}  // namespace imu_nvs
