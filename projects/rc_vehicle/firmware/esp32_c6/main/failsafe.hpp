#pragma once

#include <stdbool.h>

/**
 * Инициализация failsafe
 */
void FailsafeInit(void);

/**
 * Обновление состояния failsafe (вызывать периодически)
 * @param rc_active true если RC сигнал активен
 * @param wifi_active true если Wi-Fi команды активны
 * @return true если failsafe активен, false если система работает нормально
 */
bool FailsafeUpdate(bool rc_active, bool wifi_active);

/**
 * Проверка активности failsafe
 * @return true если failsafe активен
 */
bool FailsafeIsActive(void);

