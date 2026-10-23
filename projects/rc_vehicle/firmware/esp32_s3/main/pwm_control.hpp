#pragma once

#include <stdint.h>

#include "motor_config.hpp"

/**
 * Инициализация PWM для ESC и серво (LEDC)
 * @param motor Профиль мотора/ESC (LOS-19): диапазон, нейтраль, мёртвая зона
 *        газа. Невалидный заменяется профилем BrushedPwm по умолчанию.
 * @return 0 при успехе, -1 при ошибке
 */
int PwmControlInit(const rc_vehicle::MotorConfig& motor);

/**
 * Профиль мотора, с которым инициализирован выход газа. Меняется только при
 * загрузке: новый профиль из NVS вступает в силу после перезагрузки.
 */
const rc_vehicle::MotorConfig& PwmControlGetMotorConfig(void);

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

