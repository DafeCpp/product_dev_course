#pragma once

#include <optional>

/**
 * Инициализация RC-in (чтение сигналов с RC приёмника)
 * @return 0 при успехе, -1 при ошибке
 */
int RcInputInit(void);

/**
 * Чтение значения газа с RC приёмника
 * @return значение [-1.0..1.0] или std::nullopt при потере сигнала
 */
std::optional<float> RcInputReadThrottle(void);

/**
 * Чтение значения руля с RC приёмника
 * @return значение [-1.0..1.0] или std::nullopt при потере сигнала
 */
std::optional<float> RcInputReadSteering(void);

/**
 * Проверка наличия валидного RC сигнала
 * @return true если RC сигнал активен, false если потерян
 */
bool RcInputIsActive(void);

