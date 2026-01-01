#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * Инициализация RC-in (чтение сигналов с RC приёмника)
 * @return 0 при успехе, -1 при ошибке
 */
int RcInputInit(void);

/**
 * Чтение значения газа с RC приёмника
 * @param throttle указатель для значения [-1.0..1.0]
 * @return true если сигнал валидный, false если потерян
 */
bool RcInputReadThrottle(float *throttle);

/**
 * Чтение значения руля с RC приёмника
 * @param steering указатель для значения [-1.0..1.0]
 * @return true если сигнал валидный, false если потерян
 */
bool RcInputReadSteering(float *steering);

/**
 * Проверка наличия валидного RC сигнала
 * @return true если RC сигнал активен, false если потерян
 */
bool RcInputIsActive(void);
