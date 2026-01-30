#include "failsafe.hpp"
#include "failsafe_core.hpp"

#include "config.hpp"
#include "pico/stdlib.h"

void FailsafeInit(void) {
  rc_vehicle::FailsafeInit(FAILSAFE_TIMEOUT_MS);
}

bool FailsafeUpdate(bool rc_active, bool wifi_active) {
  uint32_t now_ms = time_us_32() / 1000;
  return rc_vehicle::FailsafeUpdate(now_ms, rc_active, wifi_active);
}

bool FailsafeIsActive(void) {
  return rc_vehicle::FailsafeIsActive();
}
