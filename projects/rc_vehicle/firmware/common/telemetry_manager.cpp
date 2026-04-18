#include "telemetry_manager.hpp"

namespace rc_vehicle {

bool TelemetryManager::Init(size_t capacity_frames) {
  return telem_log_.Init(capacity_frames);
}

void TelemetryManager::Push(const TelemetryLogFrame& frame) {
  telem_log_.Push(frame);
}

}  // namespace rc_vehicle