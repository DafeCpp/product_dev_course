#include "rc_input.hpp"

#include "config.hpp"
// TODO: реализовать на STM32Cube LL (input capture или GPIO + таймер). Пины в
// board_pins.hpp

static bool initialized = false;

int RcInputInit() {
  // TODO: настройка GPIO/таймера для измерения ширины импульсов RC
  initialized = true;
  return 0;
}

std::optional<float> RcInputReadThrottle() {
  if (!initialized) return std::nullopt;
  // TODO: прочитать ширину импульса, конвертировать в [-1..1]
  return std::nullopt;  // пока нет сигнала
}

std::optional<float> RcInputReadSteering() {
  if (!initialized) return std::nullopt;
  // TODO: прочитать ширину импульса
  return std::nullopt;
}
