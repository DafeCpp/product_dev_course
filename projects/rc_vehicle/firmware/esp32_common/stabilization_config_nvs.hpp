#pragma once

#include "esp_err.h"
#include "stabilization_config.hpp"

/**
 * @brief NVS хранилище для конфигурации стабилизации (per-mode)
 *
 * Сохраняет/загружает параметры стабилизации в/из энергонезависимой памяти
 * ESP32. Конфигурация хранится ОТДЕЛЬНО для каждого DriveMode (ключи
 * "cfg0".."cfg4"), чтобы кастомизация одного режима не затиралась при
 * переключении/сохранении другого. Дополнительно хранится номер активного
 * режима (ключ "active") — для восстановления нужного профиля при загрузке.
 * Namespace: "stab_cfg".
 */
namespace stab_config_nvs {

/**
 * @brief Загрузить конфигурацию активного (последнего сохранённого) режима
 * @param config Структура для заполнения
 * @return ESP_OK при успехе, ESP_ERR_NVS_NOT_FOUND если не найдено
 *
 * Используется при старте. Поддерживает миграцию со старого однослотового
 * формата (ключ "config").
 */
esp_err_t Load(rc_vehicle::StabilizationConfig& config);

/**
 * @brief Загрузить конфигурацию конкретного режима
 * @param mode Режим, чью конфигурацию загрузить
 * @param config Структура для заполнения
 * @return ESP_OK при успехе, ESP_ERR_NVS_NOT_FOUND если для режима нет данных
 */
esp_err_t Load(rc_vehicle::DriveMode mode,
               rc_vehicle::StabilizationConfig& config);

/**
 * @brief Сохранить конфигурацию в слот её режима (config.mode)
 * @param config Конфигурация для сохранения
 * @return ESP_OK при успехе
 *
 * Пишет в слот "cfg{config.mode}" и обновляет активный режим ("active").
 */
esp_err_t Save(const rc_vehicle::StabilizationConfig& config);

/**
 * @brief Удалить все конфигурации стабилизации из NVS (все режимы + legacy)
 * @return ESP_OK при успехе
 */
esp_err_t Erase();

}  // namespace stab_config_nvs
