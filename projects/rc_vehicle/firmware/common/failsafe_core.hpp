#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * Общая логика failsafe (RC Vehicle).
 * Платформа передаёт текущее время (мс) и таймаут при инициализации;
 * состояние (rc_active, wifi_active) — при каждом обновлении.
 * Платформы вызывают эти функции и экспортируют свой C-API (FailsafeInit(void) и т.д.).
 */
namespace rc_vehicle {

/** Инициализация: сброс состояния, задание таймаута (мс). */
void FailsafeInit(uint32_t timeout_ms);

/**
 * Обновление состояния. Вызывать периодически.
 * @param now_ms текущее время в миллисекундах (от платформы)
 * @param rc_active true если RC сигнал активен
 * @param wifi_active true если Wi‑Fi команды активны
 * @return true если failsafe активен
 */
bool FailsafeUpdate(uint32_t now_ms, bool rc_active, bool wifi_active);

/** true если failsafe активен. */
bool FailsafeIsActive(void);

}  // namespace rc_vehicle
