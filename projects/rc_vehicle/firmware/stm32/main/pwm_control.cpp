#include "pwm_control.hpp"

#include "config.hpp"
// TODO: реализовать на STM32Cube LL (timer PWM, 50 Hz). Пины в board_pins.hpp

static bool initialized = false;

int PwmControlInit() {
  // TODO: LL: RCC enable TIM+GPIO, GPIO AF, TIM 50 Hz, enable OC
  initialized = true;
  return 0;
}

int PwmControlSetThrottle(float /*throttle*/) {
  if (!initialized) return -1;
  // TODO: LL_TIM_OC_SetCompareCHx
  return 0;
}

int PwmControlSetSteering(float /*steering*/) {
  if (!initialized) return -1;
  // TODO: LL_TIM_OC_SetCompareCHx
  return 0;
}

void PwmControlSetNeutral() {
  if (!initialized) return;
  // TODO: установить нейтральные значения PWM
}
