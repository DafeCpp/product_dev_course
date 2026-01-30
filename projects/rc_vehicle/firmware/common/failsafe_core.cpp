#include "failsafe_core.hpp"

namespace rc_vehicle {

static bool s_failsafe_active = false;
static uint32_t s_last_active_source_time = 0;
static uint32_t s_timeout_ms = 250;

void FailsafeInit(uint32_t timeout_ms) {
  s_failsafe_active = false;
  s_timeout_ms = timeout_ms;
  s_last_active_source_time = 0;  // платформа задаст now при первом Update
}

bool FailsafeUpdate(uint32_t now_ms, bool rc_active, bool wifi_active) {
  if (s_last_active_source_time == 0)
    s_last_active_source_time = now_ms;

  bool has_active = rc_active || wifi_active;
  if (has_active) {
    s_last_active_source_time = now_ms;
    s_failsafe_active = false;
  } else {
    if ((now_ms - s_last_active_source_time) >= s_timeout_ms)
      s_failsafe_active = true;
  }
  return s_failsafe_active;
}

bool FailsafeIsActive(void) {
  return s_failsafe_active;
}

}  // namespace rc_vehicle
