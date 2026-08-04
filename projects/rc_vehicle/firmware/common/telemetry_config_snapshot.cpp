#include "telemetry_config_snapshot.hpp"

#include <cstdlib>
#include <cstring>

#include "telemetry_log.hpp"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

namespace rc_vehicle {

TelemetryConfigSnapshot TelemetryConfigSnapshot::FromConfig(
    uint32_t ts_ms, const StabilizationConfig& cfg) noexcept {
  TelemetryConfigSnapshot out;
  out.ts_ms = ts_ms;
  float* v = out.values;
  v[0] = cfg.enabled;
  v[1] = static_cast<uint8_t>(cfg.mode);
  v[2] = cfg.fade_ms;
  v[3] = cfg.filter.madgwick_beta;
  v[4] = cfg.filter.lpf_cutoff_hz;
  v[5] = cfg.filter.imu_sample_rate_hz;
  v[6] = cfg.filter.madgwick_enabled;
  v[7] = cfg.filter.ekf_enabled;
  v[8] = cfg.filter.adaptive_beta_enabled;
  v[9] = cfg.filter.adaptive_accel_threshold_g;
  v[10] = cfg.filter.motor_model_enabled;
  v[11] = cfg.filter.motor_speed_gain;
  v[12] = cfg.filter.motor_deadzone;
  v[13] = cfg.filter.speed_meas_noise;
  v[14] = cfg.filter.nhc_enabled;
  v[15] = cfg.filter.nhc_noise;
  v[16] = cfg.filter.tilt_comp_enabled;
  v[17] = cfg.filter.tilt_corr_gain_hz;
  v[18] = cfg.filter.tilt_accel_gate_band_g;
  const PidConfig& yaw = cfg.yaw_rate.pid;
  v[19] = yaw.kp;
  v[20] = yaw.ki;
  v[21] = yaw.kd;
  v[22] = yaw.max_integral;
  v[23] = yaw.max_correction;
  v[24] = cfg.yaw_rate.steer_to_yaw_rate_dps;
  const PidConfig& slip = cfg.slip_angle.pid;
  v[25] = slip.kp;
  v[26] = slip.ki;
  v[27] = slip.kd;
  v[28] = slip.max_integral;
  v[29] = slip.max_correction;
  v[30] = cfg.slip_angle.target_deg;
  v[31] = cfg.adaptive.enabled;
  v[32] = cfg.adaptive.speed_ref_ms;
  v[33] = cfg.adaptive.scale_min;
  v[34] = cfg.adaptive.scale_max;
  v[35] = cfg.oversteer.warn_enabled;
  v[36] = cfg.oversteer.slip_thresh_deg;
  v[37] = cfg.oversteer.rate_thresh_deg_s;
  v[38] = cfg.oversteer.throttle_reduction;
  v[39] = cfg.pitch_comp.enabled;
  v[40] = cfg.pitch_comp.gain;
  v[41] = cfg.pitch_comp.max_correction;
  v[42] = cfg.kids_mode.throttle_limit;
  v[43] = cfg.kids_mode.reverse_limit;
  v[44] = cfg.kids_mode.steering_limit;
  v[45] = cfg.kids_mode.slew_throttle;
  v[46] = cfg.kids_mode.slew_steering;
  v[47] = cfg.kids_mode.anti_spin_enabled;
  v[48] = cfg.kids_mode.anti_spin_threshold_deg;
  v[49] = cfg.kids_mode.anti_spin_reduction;
  v[50] = cfg.kids_mode.accel_limit_enabled;
  v[51] = cfg.kids_mode.accel_threshold_g;
  v[52] = cfg.kids_mode.accel_limit_gain;
  v[53] = cfg.kids_mode.accel_max_reduction;
  v[54] = cfg.kids_mode.speed_limit_enabled;
  v[55] = cfg.kids_mode.max_speed_ms;
  v[56] = cfg.kids_mode.speed_limit_gain;
  v[57] = cfg.slew_throttle;
  v[58] = cfg.slew_steering;
  v[59] = cfg.steering_trim;
  v[60] = cfg.throttle_trim;
  v[61] = static_cast<uint8_t>(cfg.braking_mode);
  v[62] = cfg.brake_slew_multiplier;
  // Дописано в конец, чтобы не сдвигать индексы v[0..62] схемы v1 (LOS-286).
  v[63] = cfg.kids_mode.limiters_enabled;
  return out;
}

TelemetryConfigSnapshotLog::TelemetryConfigSnapshotLog() { (void)Init(); }

TelemetryConfigSnapshotLog::~TelemetryConfigSnapshotLog() {
  if (!storage_) return;
#ifdef ESP_PLATFORM
  heap_caps_free(storage_);
#else
  free(storage_);
#endif
}

bool TelemetryConfigSnapshotLog::Init() {
  if (storage_) return true;
#ifdef ESP_PLATFORM
  storage_ = static_cast<uint8_t*>(
      heap_caps_malloc(kStorageBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
  storage_ = static_cast<uint8_t*>(malloc(kStorageBytes));
#endif
  return storage_ != nullptr;
}

void TelemetryConfigSnapshotLog::Push(const TelemetryConfigSnapshot& snapshot) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!storage_) return;
  const uint64_t frame_sequence =
      frame_log_ ? frame_log_->TotalFramesWritten() : 0;
  if (overflow_pending_) {
    overflow_snapshot_ = snapshot;
    overflow_frame_sequence_ = frame_sequence;
    return;
  }
  if (!has_baseline_) {
    baseline_ = latest_ = snapshot;
    baseline_frame_sequence_ = latest_frame_sequence_ = frame_sequence;
    has_baseline_ = true;
    return;
  }

  DeltaHeader header{snapshot.ts_ms, 0, frame_sequence - latest_frame_sequence_,
                     0, 0};
  float changed[TelemetryConfigSnapshot::kValueCount]{};
  for (size_t i = 0; i < TelemetryConfigSnapshot::kValueCount; ++i) {
    if (std::memcmp(&snapshot.values[i], &latest_.values[i], sizeof(float)) !=
        0) {
      header.changed_mask |= uint64_t{1} << i;
      changed[header.value_count++] = snapshot.values[i];
    }
  }
  if (header.value_count == 0) return;
  const size_t size = DeltaSize(header);
  if (size > kStorageBytes - used_bytes_) {
    overflow_snapshot_ = snapshot;
    overflow_frame_sequence_ = frame_sequence;
    overflow_pending_ = true;
    return;
  }
  const StoredDeltaHeader stored{header.ts_ms, header.changed_mask,
                                 header.value_count};
  WriteBytes(write_pos_, &stored, sizeof(stored));
  uint8_t encoded_delta[10]{};
  uint64_t remaining_delta = header.frame_delta;
  do {
    uint8_t byte = remaining_delta & 0x7f;
    remaining_delta >>= 7;
    if (remaining_delta != 0) byte |= 0x80;
    encoded_delta[header.frame_delta_size++] = byte;
  } while (remaining_delta != 0);
  WriteBytes((write_pos_ + sizeof(stored)) % kStorageBytes, encoded_delta,
             header.frame_delta_size);
  WriteBytes(
      (write_pos_ + sizeof(stored) + header.frame_delta_size) % kStorageBytes,
      changed, header.value_count * sizeof(float));
  write_pos_ = (write_pos_ + size) % kStorageBytes;
  used_bytes_ += size;
  ++count_;
  latest_ = snapshot;
  latest_frame_sequence_ = frame_sequence;
}

size_t TelemetryConfigSnapshotLog::Count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return count_ + (has_baseline_ ? 1 : 0);
}

bool TelemetryConfigSnapshotLog::GetSnapshot(
    size_t idx, TelemetryConfigSnapshot& out) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!has_baseline_ || idx > count_) return false;
  out = baseline_;
  if (idx == 0) return true;
  size_t pos = read_pos_;
  float values[TelemetryConfigSnapshot::kValueCount]{};
  for (size_t i = 0; i < idx; ++i) {
    DeltaHeader header{};
    if (!ReadDelta(pos, header, values)) return false;
    ApplyDelta(out, header, values);
    pos = (pos + DeltaSize(header)) % kStorageBytes;
  }
  return true;
}

size_t TelemetryConfigSnapshotLog::CopySnapshots(TelemetryConfigSnapshot* out,
                                                 size_t max_count) const {
  if (!out || max_count == 0) return 0;
  std::lock_guard<std::mutex> lock(mutex_);
  return CopySnapshotsLocked(out, max_count);
}

size_t TelemetryConfigSnapshotLog::CopySnapshotsLocked(
    TelemetryConfigSnapshot* out, size_t max_count) const {
  const size_t available = count_ + (has_baseline_ ? 1 : 0);
  const size_t copied = max_count < available ? max_count : available;
  if (copied == 0) return 0;
  out[0] = baseline_;
  size_t pos = read_pos_;
  float values[TelemetryConfigSnapshot::kValueCount]{};
  for (size_t i = 1; i < copied; ++i) {
    DeltaHeader header{};
    if (!ReadDelta(pos, header, values)) return i;
    out[i] = out[i - 1];
    ApplyDelta(out[i], header, values);
    pos = (pos + DeltaSize(header)) % kStorageBytes;
  }
  return copied;
}

bool TelemetryConfigSnapshotLog::BeginExport(uint32_t max_ts_ms,
                                             size_t& count_out) {
  std::unique_lock<std::mutex> export_guard(export_mutex_);
  std::lock_guard<std::mutex> lock(mutex_);
  if (!has_baseline_) return false;
  export_guard_ = std::move(export_guard);
  export_pos_ = read_pos_;
  export_delta_limit_ = count_;
  export_index_ = 0;
  export_snapshot_ = baseline_;
  export_snapshot_.frame_index = 0;
  export_frame_sequence_ = baseline_frame_sequence_;
  export_generation_ = generation_;
  export_active_ = true;
  export_uses_frame_sequence_ = false;
  export_count_ = 1;
  size_t pos = read_pos_;
  float values[TelemetryConfigSnapshot::kValueCount]{};
  for (size_t i = 0; i < count_; ++i) {
    DeltaHeader header{};
    if (!ReadDelta(pos, header, values) ||
        !IsOlderOrEqual(header.ts_ms, max_ts_ms)) {
      break;
    }
    ++export_count_;
    pos = (pos + DeltaSize(header)) % kStorageBytes;
  }
  count_out = export_count_;
  return true;
}

bool TelemetryConfigSnapshotLog::BeginExportWithFrames(
    TelemetryLog& frames, size_t& frame_count_out, TelemetryLogFrame& tail_out,
    size_t& snapshot_count_out) {
  std::unique_lock<std::mutex> export_guard(export_mutex_);
  std::lock_guard<std::mutex> lock(mutex_);
  uint64_t frame_start_sequence = 0;
  if (!has_baseline_ ||
      !frames.BeginExport(frame_count_out, tail_out, frame_start_sequence)) {
    return false;
  }
  export_guard_ = std::move(export_guard);
  export_pos_ = read_pos_;
  export_delta_limit_ = count_;
  export_index_ = 0;
  export_snapshot_ = baseline_;
  export_snapshot_.frame_index = 0;
  export_frame_sequence_ = baseline_frame_sequence_;
  export_generation_ = generation_;
  export_active_ = true;
  export_count_ = 0;
  export_max_ts_ms_ = tail_out.ts_ms;
  export_frame_start_sequence_ = frame_start_sequence;
  export_frame_end_sequence_ = frame_start_sequence + frame_count_out;
  export_uses_frame_sequence_ = frame_log_ != nullptr;
  export_has_frames_ = frame_count_out > 0;
  snapshot_count_out = 0;
  return true;
}

bool TelemetryConfigSnapshotLog::FinalizeExportWithFrameBoundary(
    size_t& snapshot_count_out) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!export_active_) return false;
  if (!export_has_frames_) {
    snapshot_count_out = 0;
    return true;
  }

  export_count_ = 1;
  size_t pos = export_pos_;
  uint64_t frame_sequence = baseline_frame_sequence_;
  float values[TelemetryConfigSnapshot::kValueCount]{};
  for (size_t i = 0; i < export_delta_limit_; ++i) {
    DeltaHeader header{};
    if (!ReadDelta(pos, header, values)) break;
    frame_sequence += header.frame_delta;
    const bool is_in_export =
        export_uses_frame_sequence_
            ? frame_sequence < export_frame_end_sequence_
            : IsOlderOrEqual(header.ts_ms, export_max_ts_ms_);
    if (!is_in_export) {
      break;
    }
    ++export_count_;
    pos = (pos + DeltaSize(header)) % kStorageBytes;
  }
  snapshot_count_out = export_count_;
  return true;
}

bool TelemetryConfigSnapshotLog::GetNextExportSnapshot(
    TelemetryConfigSnapshot& out) {
  if (!export_active_ || export_index_ >= export_count_) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  if (generation_ != export_generation_) return false;
  if (export_index_ == 0) {
    out = export_snapshot_;
  } else {
    DeltaHeader header{};
    float values[TelemetryConfigSnapshot::kValueCount]{};
    if (!ReadDelta(export_pos_, header, values)) return false;
    ApplyDelta(export_snapshot_, header, values);
    export_frame_sequence_ += header.frame_delta;
    export_pos_ = (export_pos_ + DeltaSize(header)) % kStorageBytes;
    out = export_snapshot_;
    if (export_uses_frame_sequence_) {
      const uint64_t relative =
          export_frame_sequence_ > export_frame_start_sequence_
              ? export_frame_sequence_ - export_frame_start_sequence_
              : 0;
      out.frame_index = static_cast<uint32_t>(relative);
    }
  }
  ++export_index_;
  return true;
}

void TelemetryConfigSnapshotLog::EndExport() {
  std::lock_guard<std::mutex> lock(mutex_);
  export_active_ = false;
  export_guard_.unlock();
}

bool TelemetryConfigSnapshotLog::ResetAfterOverflow() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!overflow_pending_ || export_active_) return false;
  read_pos_ = 0;
  write_pos_ = 0;
  used_bytes_ = 0;
  count_ = 0;
  baseline_ = latest_ = overflow_snapshot_;
  baseline_frame_sequence_ = latest_frame_sequence_ = overflow_frame_sequence_;
  has_baseline_ = true;
  overflow_pending_ = false;
  ++generation_;
  return true;
}

void TelemetryConfigSnapshotLog::PruneBefore(uint32_t oldest_frame_ts_ms) {
  PruneBefore(oldest_frame_ts_ms, 0);
}

void TelemetryConfigSnapshotLog::PruneBefore(uint32_t oldest_frame_ts_ms,
                                             uint64_t oldest_frame_sequence) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (export_active_) return;
  while (count_ > 0) {
    DeltaHeader header{};
    float values[TelemetryConfigSnapshot::kValueCount]{};
    if (!ReadDelta(read_pos_, header, values)) break;
    const uint64_t frame_sequence =
        baseline_frame_sequence_ + header.frame_delta;
    const bool is_before_oldest =
        frame_log_ ? frame_sequence <= oldest_frame_sequence
                   : IsOlderOrEqual(header.ts_ms, oldest_frame_ts_ms);
    if (!is_before_oldest) {
      break;
    }
    ApplyDelta(baseline_, header, values);
    baseline_frame_sequence_ = frame_sequence;
    const size_t size = DeltaSize(header);
    read_pos_ = (read_pos_ + size) % kStorageBytes;
    used_bytes_ -= size;
    --count_;
  }
}

size_t TelemetryConfigSnapshotLog::DeltaSize(const DeltaHeader& header) const {
  uint64_t delta = header.frame_delta;
  size_t encoded_size = 1;
  while ((delta >>= 7) != 0) ++encoded_size;
  return sizeof(StoredDeltaHeader) + encoded_size +
         header.value_count * sizeof(float);
}

void TelemetryConfigSnapshotLog::ReadBytes(size_t pos, void* out,
                                           size_t size) const {
  const size_t first = size < kStorageBytes - pos ? size : kStorageBytes - pos;
  std::memcpy(out, storage_ + pos, first);
  if (first < size) {
    std::memcpy(static_cast<uint8_t*>(out) + first, storage_, size - first);
  }
}

void TelemetryConfigSnapshotLog::WriteBytes(size_t pos, const void* data,
                                            size_t size) {
  const size_t first = size < kStorageBytes - pos ? size : kStorageBytes - pos;
  std::memcpy(storage_ + pos, data, first);
  if (first < size) {
    std::memcpy(storage_, static_cast<const uint8_t*>(data) + first,
                size - first);
  }
}

bool TelemetryConfigSnapshotLog::ReadDelta(size_t pos, DeltaHeader& header,
                                           float* values) const {
  if (!storage_) return false;
  StoredDeltaHeader stored{};
  ReadBytes(pos, &stored, sizeof(stored));
  header.ts_ms = stored.ts_ms;
  header.changed_mask = stored.changed_mask;
  header.value_count = stored.value_count;
  if (header.value_count > TelemetryConfigSnapshot::kValueCount) return false;
  header.frame_delta = 0;
  header.frame_delta_size = 0;
  for (uint8_t shift = 0; shift < 64; shift += 7) {
    uint8_t byte = 0;
    ReadBytes((pos + sizeof(stored) + header.frame_delta_size) % kStorageBytes,
              &byte, 1);
    ++header.frame_delta_size;
    header.frame_delta |= static_cast<uint64_t>(byte & 0x7f) << shift;
    if ((byte & 0x80) == 0) break;
    if (shift >= 63) return false;
  }
  ReadBytes((pos + sizeof(stored) + header.frame_delta_size) % kStorageBytes,
            values, header.value_count * sizeof(float));
  return true;
}

void TelemetryConfigSnapshotLog::ApplyDelta(TelemetryConfigSnapshot& snapshot,
                                            const DeltaHeader& header,
                                            const float* values) const {
  size_t value_idx = 0;
  for (size_t i = 0; i < TelemetryConfigSnapshot::kValueCount; ++i) {
    if ((header.changed_mask & (uint64_t{1} << i)) != 0) {
      snapshot.values[i] = values[value_idx++];
    }
  }
  snapshot.ts_ms = header.ts_ms;
}

bool TelemetryConfigSnapshotLog::IsOlderOrEqual(uint32_t lhs,
                                                uint32_t rhs) noexcept {
  return static_cast<int32_t>(lhs - rhs) <= 0;
}

void TelemetryConfigSnapshotLog::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  write_pos_ = 0;
  read_pos_ = 0;
  used_bytes_ = 0;
  count_ = 0;
  has_baseline_ = false;
  overflow_pending_ = false;
  ++generation_;
}

}  // namespace rc_vehicle
