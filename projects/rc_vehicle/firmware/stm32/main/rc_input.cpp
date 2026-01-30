#include "rc_input.hpp"
#include "config.hpp"
// TODO: реализовать на STM32Cube LL (input capture или GPIO + таймер). Пины в board_pins.hpp

static bool initialized = false;

int RcInputInit(void) {
  // TODO: настройка GPIO/таймера для измерения ширины импульсов RC
  initialized = true;
  return 0;
}

bool RcInputReadThrottle(float *throttle) {
  if (!initialized || !throttle)
    return false;
  *throttle = 0.0f;
  // TODO: прочитать ширину импульса, конвертировать в [-1..1]
  return false; // пока нет сигнала
}

bool RcInputReadSteering(float *steering) {
  if (!initialized || !steering)
    return false;
  *steering = 0.0f;
  // TODO: прочитать ширину импульса
  return false;
}
