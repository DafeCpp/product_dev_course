#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>

#include "stabilization_config.hpp"

class TelemetryLog;
struct TelemetryLogFrame;

namespace rc_vehicle {

/** Versioned, fixed wire representation of a stabilization configuration. */
struct TelemetryConfigSnapshot {
  // v2: добавлен kids_mode.limiters_enabled как values[63] (LOS-286)
  static constexpr uint16_t kSchemaVersion = 2;
  static constexpr size_t kValueCount = 64;

  uint32_t ts_ms{0};
  uint16_t schema_version{kSchemaVersion};
  uint16_t value_count{kValueCount};
  float values[kValueCount]{};
  uint32_t frame_index{0};

  [[nodiscard]] static TelemetryConfigSnapshot FromConfig(
      uint32_t ts_ms, const StabilizationConfig& cfg) noexcept;
};
static_assert(sizeof(TelemetryConfigSnapshot) == 268,
              "TelemetryConfigSnapshot size mismatch");
// changed_mask дельты — uint64_t, по биту на значение. 64 значения занимают
// маску целиком: следующее поле потребует расширения маски, а не только
// инкремента kValueCount.
static_assert(TelemetryConfigSnapshot::kValueCount <= 64,
              "changed_mask (uint64_t) cannot address more than 64 values");

/** Sparse configuration snapshots aligned to the retained frame history. */
class TelemetryConfigSnapshotLog {
 public:
  // 1 MB byte-ring stores a full baseline plus variable-length deltas.
  // Typical one-field transitions take 18 bytes instead of 264 bytes.
  static constexpr size_t kStorageBytes = 1024 * 1024;

  TelemetryConfigSnapshotLog();
  ~TelemetryConfigSnapshotLog();
  [[nodiscard]] bool Init();
  void SetFrameLog(TelemetryLog* frame_log) { frame_log_ = frame_log; }

  void Push(const TelemetryConfigSnapshot& snapshot);
  [[nodiscard]] size_t Count() const;
  [[nodiscard]] bool GetSnapshot(size_t idx,
                                 TelemetryConfigSnapshot& out) const;
  /** Copy a stable, oldest-to-newest sequence for external serialization. */
  [[nodiscard]] size_t CopySnapshots(TelemetryConfigSnapshot* out,
                                     size_t max_count) const;
  [[nodiscard]] bool BeginExport(uint32_t max_ts_ms, size_t& count_out);
  [[nodiscard]] bool BeginExportWithFrames(TelemetryLog& frames,
                                           size_t& frame_count_out,
                                           TelemetryLogFrame& tail_out,
                                           size_t& snapshot_count_out);
  [[nodiscard]] bool FinalizeExportWithFrameBoundary(
      size_t& snapshot_count_out);
  [[nodiscard]] bool GetNextExportSnapshot(TelemetryConfigSnapshot& out);
  void EndExport();
  /** Atomically reset the delta ring and seed its pending overflow snapshot. */
  [[nodiscard]] bool ResetAfterOverflow();
  /**
   * Drop snapshots older than the retained frame range while preserving the
   * last one as its baseline configuration.
   */
  void PruneBefore(uint32_t oldest_frame_ts_ms);
  void PruneBefore(uint32_t oldest_frame_ts_ms, uint64_t oldest_frame_sequence);
  void Clear();

 private:
  struct DeltaHeader {
    uint32_t ts_ms;
    uint64_t changed_mask;
    uint64_t frame_delta;
    uint8_t value_count;
    uint8_t frame_delta_size;
  };
  struct StoredDeltaHeader {
    uint32_t ts_ms;
    uint64_t changed_mask;
    uint8_t value_count;
  } __attribute__((packed));
  static_assert(sizeof(StoredDeltaHeader) == 13);

  [[nodiscard]] static bool IsOlderOrEqual(uint32_t lhs, uint32_t rhs) noexcept;
  size_t CopySnapshotsLocked(TelemetryConfigSnapshot* out,
                             size_t max_count) const;
  void ReadBytes(size_t pos, void* out, size_t size) const;
  void WriteBytes(size_t pos, const void* data, size_t size);
  [[nodiscard]] bool ReadDelta(size_t pos, DeltaHeader& header,
                               float* values) const;
  [[nodiscard]] size_t DeltaSize(const DeltaHeader& header) const;
  void ApplyDelta(TelemetryConfigSnapshot& snapshot, const DeltaHeader& header,
                  const float* values) const;

  uint8_t* storage_{nullptr};
  TelemetryLog* frame_log_{nullptr};
  size_t read_pos_{0};
  size_t write_pos_{0};
  size_t used_bytes_{0};
  size_t count_{0};
  TelemetryConfigSnapshot baseline_{};
  TelemetryConfigSnapshot latest_{};
  TelemetryConfigSnapshot overflow_snapshot_{};
  uint64_t baseline_frame_sequence_{0};
  uint64_t latest_frame_sequence_{0};
  uint64_t overflow_frame_sequence_{0};
  bool has_baseline_{false};
  bool overflow_pending_{false};
  mutable std::mutex mutex_;
  std::mutex export_mutex_;
  std::unique_lock<std::mutex> export_guard_{};
  size_t export_pos_{0};
  size_t export_delta_limit_{0};
  size_t export_index_{0};
  size_t export_count_{0};
  uint32_t export_max_ts_ms_{0};
  uint64_t export_frame_start_sequence_{0};
  uint64_t export_frame_end_sequence_{0};
  uint64_t export_frame_sequence_{0};
  bool export_uses_frame_sequence_{false};
  bool export_has_frames_{false};
  TelemetryConfigSnapshot export_snapshot_{};
  uint32_t generation_{0};
  uint32_t export_generation_{0};
  bool export_active_{false};
};

}  // namespace rc_vehicle
