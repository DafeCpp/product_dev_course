#include "failsafe.hpp"

#include "config.hpp"
#include "failsafe_core.hpp"
#include "platform.hpp"

void FailsafeInit(void) { rc_vehicle::FailsafeInit(FAILSAFE_TIMEOUT_MS); }

bool FailsafeUpdate(bool rc_active, bool wifi_active) {
  uint32_t now_ms = PlatformGetTimeMs();
  return rc_vehicle::FailsafeUpdate(now_ms, rc_active, wifi_active);
}

bool FailsafeIsActive(void) { return rc_vehicle::FailsafeIsActive(); }
