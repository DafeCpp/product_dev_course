#include "telemetry_log.hpp"

#include <cstdlib>
#include <mutex>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

TelemetryLog::~TelemetryLog() {
  if (buf_) {
#ifdef ESP_PLATFORM
    heap_caps_free(buf_);
#else
    free(buf_);
#endif
    buf_ = nullptr;
  }
}

bool TelemetryLog::Init(size_t capacity_frames) {
  if (capacity_frames == 0) {
    return false;
  }

  const size_t bytes = capacity_frames * sizeof(TelemetryLogFrame);

#ifdef ESP_PLATFORM
  // Пробуем выделить из PSRAM; при отказе — fallback на обычную heap
  buf_ = static_cast<TelemetryLogFrame*>(
      heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!buf_) {
    buf_ = static_cast<TelemetryLogFrame*>(malloc(bytes));
  }
#else
  buf_ = static_cast<TelemetryLogFrame*>(malloc(bytes));
#endif

  if (!buf_) {
    return false;
  }

  capacity_ = capacity_frames;
  count_ = 0;
  write_pos_ = 0;
  total_frames_written_ = 0;
  return true;
}

size_t TelemetryLog::Count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return count_;
}

void TelemetryLog::Push(const TelemetryLogFrame& frame) {
  if (!buf_ || capacity_ == 0) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  buf_[write_pos_ % capacity_] = frame;
  write_pos_++;
  total_frames_written_++;
  if (count_ < capacity_) {
    count_++;
  }
}

bool TelemetryLog::GetFrame(size_t idx, TelemetryLogFrame& out) const {
  if (!buf_) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (idx >= count_) {
    return false;
  }
  // Oldest frame находится по индексу: (write_pos_ - count_ + idx) % capacity_
  const size_t real_pos = (write_pos_ - count_ + idx) % capacity_;
  out = buf_[real_pos];
  return true;
}

bool TelemetryLog::BeginExport(size_t& count_out) {
  TelemetryLogFrame ignored_tail{};
  uint64_t ignored_sequence = 0;
  return BeginExport(count_out, ignored_tail, ignored_sequence);
}

bool TelemetryLog::BeginExport(size_t& count_out, TelemetryLogFrame& tail_out) {
  uint64_t ignored_sequence = 0;
  return BeginExport(count_out, tail_out, ignored_sequence);
}

bool TelemetryLog::BeginExport(size_t& count_out, TelemetryLogFrame& tail_out,
                               uint64_t& start_sequence_out) {
  std::unique_lock<std::mutex> export_guard(export_mutex_);
  if (!buf_) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  export_guard_ = std::move(export_guard);
  export_count_ = count_;
  export_start_pos_ = write_pos_ - count_;
  export_end_write_pos_ = write_pos_;
  export_active_ = true;
  start_sequence_out = total_frames_written_ - count_;
  if (export_count_ > 0) {
    tail_out = buf_[(write_pos_ - 1) % capacity_];
  }
  count_out = export_count_;
  return true;
}

uint64_t TelemetryLog::TotalFramesWritten() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return total_frames_written_;
}

bool TelemetryLog::GetOldestFrameBoundary(TelemetryLogFrame& frame_out,
                                          uint64_t& sequence_out) const {
  if (!buf_) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  if (count_ == 0) return false;
  frame_out = buf_[(write_pos_ - count_) % capacity_];
  sequence_out = total_frames_written_ - count_;
  return true;
}

size_t TelemetryLog::CopyExportFrames(size_t start_idx, TelemetryLogFrame* out,
                                      size_t max_count) const {
  if (!out || !export_active_ || start_idx >= export_count_) return 0;
  std::lock_guard<std::mutex> lock(mutex_);
  if (write_pos_ < export_end_write_pos_) return 0;
  const size_t writes_since_export = write_pos_ - export_end_write_pos_;
  const size_t overwrite_after = capacity_ - export_count_ + start_idx;
  if (writes_since_export > overwrite_after) {
    return 0;
  }
  const size_t remaining = export_count_ - start_idx;
  const size_t copied = max_count < remaining ? max_count : remaining;
  for (size_t i = 0; i < copied; ++i) {
    const size_t real_pos = (export_start_pos_ + start_idx + i) % capacity_;
    out[i] = buf_[real_pos];
  }
  return copied;
}

void TelemetryLog::EndExport() {
  export_count_ = 0;
  export_active_ = false;
  export_guard_.unlock();
}

void TelemetryLog::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  count_ = 0;
  write_pos_ = 0;
}
