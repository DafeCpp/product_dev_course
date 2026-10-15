#pragma once

#include <optional>

#include "rc_pulse_decoder.hpp"

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

/**
 * Счётчики декодеров и активность обоих каналов (LOS-216): сколько импульсов
 * принято/отброшено (короткие/длинные), последние ширины. Нужны, чтобы при
 * потере RC отличить «импульсов нет» (провод, питание, бинд) от «импульсы
 * есть, но вне окна приёма» (EPA/триммер, помеха).
 */
rc_vehicle::RcInputStats RcInputGetStats(void);
