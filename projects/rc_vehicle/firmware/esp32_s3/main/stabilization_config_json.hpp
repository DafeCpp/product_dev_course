#pragma once

#include "cJSON.h"
#include "stabilization_config.hpp"

/**
 * @brief Сериализовать StabilizationConfig в cJSON-объект.
 *
 * Создаёт новый cJSON-объект со всеми полями конфигурации.
 * Вызывающий код обязан вызвать cJSON_Delete() на возвращённом объекте.
 *
 * @param cfg Конфигурация стабилизации
 * @return Новый cJSON-объект или nullptr при ошибке выделения памяти
 */
cJSON* StabilizationConfigToJson(const rc_vehicle::StabilizationConfig& cfg);

/**
 * @brief Обновить поля StabilizationConfig из cJSON-объекта.
 *
 * Выполняет частичное обновление: изменяются только те поля,
 * которые присутствуют в json. Отсутствующие поля не трогаются.
 *
 * @param cfg  Конфигурация стабилизации (изменяется на месте)
 * @param json Входной JSON-объект (может содержать произвольное подмножество полей)
 */
void StabilizationConfigFromJson(rc_vehicle::StabilizationConfig& cfg,
                                 const cJSON* json);
