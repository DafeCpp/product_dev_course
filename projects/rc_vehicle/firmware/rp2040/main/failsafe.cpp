#include "failsafe.hpp"

#include "config.hpp"
#include "pico/stdlib.h"

static bool failsafe_active = false;
static uint32_t last_active_source_time = 0;

void FailsafeInit(void) {
  failsafe_active = false;
  last_active_source_time = time_us_32() / 1000; // в миллисекундах
}

bool FailsafeUpdate(bool rc_active, bool wifi_active) {
  uint32_t now = time_us_32() / 1000; // в миллисекундах

  // Проверка наличия активного источника управления
  // RC имеет приоритет над Wi-Fi
  bool has_active_source = rc_active || wifi_active;

  if (has_active_source) {
    last_active_source_time = now;
    failsafe_active = false;
  } else {
    // Проверка таймаута
    uint32_t time_since_last_source = now - last_active_source_time;
    if (time_since_last_source >= FAILSAFE_TIMEOUT_MS) {
      failsafe_active = true;
    }
  }

  return failsafe_active;
}

bool FailsafeIsActive(void) { return failsafe_active; }
