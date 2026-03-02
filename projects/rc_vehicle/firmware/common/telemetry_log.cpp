#include "telemetry_log.hpp"

#include <cstdlib>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

TelemetryLog::~TelemetryLog() {
  if (buf_) {
#ifdef ESP_PLATFORM
    free(buf_);
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
  count_.store(0);
  write_pos_.store(0);
  return true;
}

void TelemetryLog::Push(const TelemetryLogFrame& frame) {
  if (!buf_ || capacity_ == 0) {
    return;
  }

  const size_t pos = write_pos_.fetch_add(1) % capacity_;
  buf_[pos] = frame;

  // Увеличиваем count до capacity_ максимум
  size_t current = count_.load();
  if (current < capacity_) {
    count_.fetch_add(1);
  }
}

bool TelemetryLog::GetFrame(size_t idx, TelemetryLogFrame& out) const {
  if (!buf_ || idx >= count_.load()) {
    return false;
  }

  const size_t cnt = count_.load();
  const size_t wpos = write_pos_.load();
  // Oldest frame находится по индексу: (wpos - cnt + idx) % capacity_
  const size_t real_pos = (wpos - cnt + idx) % capacity_;
  out = buf_[real_pos];
  return true;
}

void TelemetryLog::Clear() {
  count_.store(0);
  write_pos_.store(0);
}
