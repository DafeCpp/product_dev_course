#pragma once

#include "esp_err.h"
#include "motor_config.hpp"

/**
 * NVS-хранение профиля мотора/ESC (LOS-19).
 *
 * Отдельный blob, а не поле StabilizationConfig: профиль относится к железу
 * и не должен меняться вместе с режимом езды.
 *
 * NVS namespace: "motor_cfg"
 * Ключ:          "cfg"
 */
namespace motor_config_nvs {

/**
 * Загрузить конфигурацию.
 * Возвращает ESP_ERR_NOT_FOUND, если записи нет, формат неизвестен или
 * значения не прошли MotorConfig::IsValid (тогда config не меняется).
 */
esp_err_t Load(rc_vehicle::MotorConfig& config);

/**
 * Сохранить конфигурацию. Невалидную не пишет: ESP_ERR_INVALID_ARG.
 */
esp_err_t Save(const rc_vehicle::MotorConfig& config);

/** Удалить запись (следующая загрузка вернёт профиль по умолчанию). */
esp_err_t Erase();

}  // namespace motor_config_nvs
