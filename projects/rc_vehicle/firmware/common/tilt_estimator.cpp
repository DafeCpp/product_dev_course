#include "tilt_estimator.hpp"

#include <cmath>

#include "imu_sensor.hpp"

namespace rc_vehicle {

namespace {
constexpr float kPi = 3.14159265358979f;
constexpr float kDegToRad = kPi / 180.0f;
}  // namespace

void TiltEstimator::Reset() noexcept {
  pitch_rad_ = 0.0f;
  roll_rad_ = 0.0f;
}

void TiltEstimator::Update(const ImuData& imu, float a_lin_long_g,
                           float dt_sec) noexcept {
  if (dt_sec <= 0.0f) {
    return;
  }

  // ── 1) Гиро-пропагация: иммунна к линейному ускорению ──────────────────
  // Малоугловое приближение: pitch — вокруг Y тела, roll — вокруг X тела.
  pitch_rad_ = ClampTilt(pitch_rad_ + imu.gy * kDegToRad * dt_sec);
  roll_rad_ = ClampTilt(roll_rad_ + imu.gx * kDegToRad * dt_sec);

  // ── 2) Accel-коррекция со снятием известного линейного ускорения ───────
  // Остаток ax_grav ≈ чистая гравитационная проекция, если a_lin_long_g
  // верно оценивает продольное ускорение (см. Update() в вызывающем коде).
  const float ax_grav = imu.ax - a_lin_long_g;
  const float ay = imu.ay;
  const float az = imu.az;
  const float accel_mag = std::sqrt(ax_grav * ax_grav + ay * ay + az * az);

  // ── 3) Гейт: коррекция только когда скорректированный вектор близок к
  // 1g. Вне диапазона (удар/выброс на ухабе, либо a_lin_long_g неточен) —
  // акселерометру не доверяем, тангаж временно держится на гиро.
  if (std::abs(accel_mag - 1.0f) > params_.accel_gate_band_g) {
    return;
  }

  const float horiz = std::sqrt(ay * ay + az * az);
  const float pitch_acc = std::atan2(-ax_grav, horiz);
  const float roll_acc = std::atan2(ay, az);

  const float k = params_.corr_gain_hz * dt_sec;
  pitch_rad_ = ClampTilt(pitch_rad_ + k * WrapAngle(pitch_acc - pitch_rad_));
  roll_rad_ = ClampTilt(roll_rad_ + k * WrapAngle(roll_acc - roll_rad_));
}

float TiltEstimator::WrapAngle(float a) noexcept {
  while (a > kPi) a -= 2.0f * kPi;
  while (a < -kPi) a += 2.0f * kPi;
  return a;
}

float TiltEstimator::ClampTilt(float a) noexcept {
  if (a > kMaxTiltRad) return kMaxTiltRad;
  if (a < -kMaxTiltRad) return -kMaxTiltRad;
  return a;
}

}  // namespace rc_vehicle
