#pragma once

#include <stdint.h>

/**
 * Инициализация PWM для ESC и серво (LEDC)
 * @return 0 при успехе, -1 при ошибке
 */
int PwmControlInit(void);

/**
 * Установить значение PWM для газа (ESC)
 * @param throttle значение в диапазоне [-1.0..1.0]
 * @return 0 при успехе, -1 при ошибке
 */
int PwmControlSetThrottle(float throttle);

/**
 * Установить значение PWM для руля (серво)
 * @param steering значение в диапазоне [-1.0..1.0]
 * @return 0 при успехе, -1 при ошибке
 */
int PwmControlSetSteering(float steering);

/**
 * Установить нейтральные значения (failsafe)
 */
void PwmControlSetNeutral(void);

