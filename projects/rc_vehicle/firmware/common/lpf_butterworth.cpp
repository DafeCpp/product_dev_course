#include "lpf_butterworth.hpp"

#include <cmath>

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kSqrt2 = 1.41421356237309504880f;  // 1/sqrt(2) for Butterworth

}  // namespace

LpfButterworth2::LpfButterworth2() {
  Reset();
}

void LpfButterworth2::SetParams(float cutoff_hz, float sample_rate_hz) {
  if (cutoff_hz <= 0.f || sample_rate_hz <= 0.f ||
      cutoff_hz >= sample_rate_hz / 2.f) {
    configured_ = false;
    return;
  }
  cutoff_hz_ = cutoff_hz;
  sample_rate_hz_ = sample_rate_hz;
  configured_ = true;
  UpdateCoefficients();
  Reset();
}

void LpfButterworth2::UpdateCoefficients() {
  // Digital Butterworth 2nd order LPF via bilinear transform.
  // K = tan(pi * fc / fs), Q = 1/sqrt(2)
  // norm = 1 + K/Q + K^2
  // b0 = K^2/norm, b1 = 2*b0, b2 = b0
  // a0 = 1, a1 = 2*(K^2 - 1)/norm, a2 = (1 - K/Q + K^2)/norm
  const float fc = cutoff_hz_;
  const float fs = sample_rate_hz_;
  const float K = std::tan(kPi * fc / fs);
  const float Q = kSqrt2;
  const float K2 = K * K;
  const float norm = 1.f + K / Q + K2;

  b0_ = K2 / norm;
  b1_ = 2.f * b0_;
  b2_ = b0_;
  a1_ = 2.f * (K2 - 1.f) / norm;
  a2_ = (1.f - K / Q + K2) / norm;
}

float LpfButterworth2::Step(float x) {
  if (!configured_) {
    y1_ = x;
    return x;
  }
  // y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
  const float y = b0_ * x + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;

  x2_ = x1_;
  x1_ = x;
  y2_ = y1_;
  y1_ = y;

  return y;
}

void LpfButterworth2::Reset() {
  x1_ = x2_ = 0.f;
  y1_ = y2_ = 0.f;
}
