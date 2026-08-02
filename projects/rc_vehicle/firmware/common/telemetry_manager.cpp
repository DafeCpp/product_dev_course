#include "telemetry_manager.hpp"

namespace rc_vehicle {

bool TelemetryManager::Init(size_t capacity_frames) {
  return telem_log_.Init(capacity_frames) && config_snapshots_.Init();
}

void TelemetryManager::Push(const TelemetryLogFrame& frame) {
  if (config_snapshots_.ResetAfterOverflow()) {
    // The delta ring can no longer describe the old frame history. Drop that
    // history before recording another frame. ResetAfterOverflow() has already
    // seeded the new config atomically with the delta-ring reset.
    telem_log_.Clear();
    event_log_.Clear();
  }
  telem_log_.Push(frame);
  TelemetryLogFrame oldest{};
  uint64_t oldest_sequence = 0;
  if (telem_log_.GetOldestFrameBoundary(oldest, oldest_sequence)) {
    config_snapshots_.PruneBefore(oldest.ts_ms, oldest_sequence);
  }
}

}  // namespace rc_vehicle
