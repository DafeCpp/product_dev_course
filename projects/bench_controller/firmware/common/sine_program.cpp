#include "sine_program.hpp"

#include <cmath>

namespace bench {

namespace {
constexpr float kTwoPi = 6.28318530717958647692f;
}  // namespace

SineProgram::SineProgram(std::span<const Segment> segments) {
  count_ = segments.size() < kMaxSegments ? segments.size() : kMaxSegments;
  for (size_t i = 0; i < count_; ++i) {
    segments_[i] = segments[i];
  }
  finished_ = count_ == 0;
}

float SineProgram::Step(float dt_sec) noexcept {
  if (finished_) {
    return count_ > 0 ? segments_[count_ - 1].mean : 0.0f;
  }

  const Segment& seg = segments_[index_];
  phase_ += kTwoPi * seg.freq_hz * dt_sec;
  if (phase_ >= kTwoPi) {
    phase_ -= kTwoPi;
    ++cycles_done_;
    if (cycles_done_ >= seg.cycles) {
      cycles_done_ = 0;
      phase_ = 0.0f;
      ++index_;
      if (index_ >= count_) {
        finished_ = true;
        return segments_[count_ - 1].mean;
      }
    }
  }

  const Segment& cur = segments_[index_];
  return cur.mean + cur.amplitude * std::sin(phase_);
}

float SineProgram::CurrentMean() const noexcept {
  if (count_ == 0) return 0.0f;
  return segments_[finished_ ? count_ - 1 : index_].mean;
}

}  // namespace bench
