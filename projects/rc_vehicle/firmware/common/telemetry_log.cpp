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

void TelemetryLog::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  count_ = 0;
  write_pos_ = 0;
}
