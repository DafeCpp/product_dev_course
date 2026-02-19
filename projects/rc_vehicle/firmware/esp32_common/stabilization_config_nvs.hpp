#pragma once

#include "esp_err.h"
#include "stabilization_config.hpp"

/**
 * @brief NVS хранилище для конфигурации стабилизации
 *
 * Сохраняет/загружает параметры стабилизации в/из энергонезависимой памяти
 * ESP32. Namespace: "stab_cfg" Key: "config"
 */
namespace stab_config_nvs {

/**
 * @brief Загрузить конфигурацию стабилизации из NVS
 * @param config Структура для заполнения
 * @return ESP_OK при успехе, ESP_ERR_NVS_NOT_FOUND если не найдено
 */
esp_err_t Load(StabilizationConfig& config);

/**
 * @brief Сохранить конфигурацию стабилизации в NVS
 * @param config Конфигурация для сохранения
 * @return ESP_OK при успехе
 */
esp_err_t Save(const StabilizationConfig& config);

/**
 * @brief Удалить конфигурацию стабилизации из NVS
 * @return ESP_OK при успехе
 */
esp_err_t Erase();

}  // namespace stab_config_nvs