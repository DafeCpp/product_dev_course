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
esp_err_t Save(const rc_vehicle::ImuCalibData& data);

/** Загрузить калибровочные данные из NVS.
 *  Возвращает ESP_ERR_NOT_FOUND если данных нет или формат устарел. */
esp_err_t Load(rc_vehicle::ImuCalibData& data);

/** Удалить калибровочные данные из NVS (сброс). */
esp_err_t Erase();

/** Сохранить смещение IMU→CoM в NVS (отдельный ключ "com_off"). */
esp_err_t SaveComOffset(const float offset[2]);

/** Загрузить смещение IMU→CoM из NVS.
 *  Возвращает ESP_ERR_NOT_FOUND если данных нет. */
esp_err_t LoadComOffset(float offset[2]);

}  // namespace imu_nvs
