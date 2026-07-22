#include <gtest/gtest.h>

#include <cmath>

#include "imu_sensor.hpp"
#include "tilt_estimator.hpp"

using namespace rc_vehicle;

namespace {
constexpr float kPiF = 3.14159265358979f;
constexpr float kDt = 0.002f;  // 500 Hz

float DegToRad(float deg) { return deg * kPiF / 180.0f; }
float RadToDeg(float rad) { return rad * 180.0f / kPiF; }
}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Гиро-пропагация: разгон на ровном не заваливает pitch (ключевой failure
// mode LOS-240 — Madgwick заваливал тангаж на ~11° при 0.2g разгоне).
// ═══════════════════════════════════════════════════════════════════════════

TEST(TiltEstimatorTest, LevelAccel_NoTiltGrowth) {
  TiltEstimator est;
  ImuData imu{};
  imu.gx = 0.0f;
  imu.gy = 0.0f;  // корпус не наклоняется
  imu.ay = 0.0f;

  // Плавный разгон 0.2g: |a| = sqrt(0.2^2+1^2) ≈ 1.0198g — если бы гейт был
  // по |a|-1g > 0.1, коррекция бы включилась и увела тангаж.
  const float a_lin_g = 0.2f;
  imu.ax = a_lin_g;
  imu.az = 1.0f;

  for (int i = 0; i < 2500; ++i) {  // 5 секунд
    est.Update(imu, a_lin_g, kDt);
  }

  EXPECT_NEAR(RadToDeg(est.GetPitchRad()), 0.0f, 1.0f);
  EXPECT_NEAR(RadToDeg(est.GetRollRad()), 0.0f, 1.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Статический наклон: accel-коррекция должна сойтись к верному углу.
// ═══════════════════════════════════════════════════════════════════════════

TEST(TiltEstimatorTest, StaticPitch_ConvergesToTrueAngle) {
  TiltEstimator est;
  ImuData imu{};
  imu.gx = imu.gy = imu.gz = 0.0f;  // машина неподвижна и не вращается

  const float pitch_true = DegToRad(20.0f);
  imu.ax = -std::sin(pitch_true);
  imu.ay = 0.0f;
  imu.az = std::cos(pitch_true);

  for (int i = 0; i < 5000; ++i) {  // 10 секунд — дать сойтись комплементарке
    est.Update(imu, 0.0f, kDt);
  }

  EXPECT_NEAR(RadToDeg(est.GetPitchRad()), 20.0f, 1.0f);
  EXPECT_NEAR(RadToDeg(est.GetRollRad()), 0.0f, 1.0f);
}

TEST(TiltEstimatorTest, StaticRoll_ConvergesToTrueAngle) {
  TiltEstimator est;
  ImuData imu{};
  imu.gx = imu.gy = imu.gz = 0.0f;

  const float roll_true = DegToRad(15.0f);
  imu.ax = 0.0f;
  imu.ay = std::sin(roll_true);
  imu.az = std::cos(roll_true);

  for (int i = 0; i < 5000; ++i) {
    est.Update(imu, 0.0f, kDt);
  }

  EXPECT_NEAR(RadToDeg(est.GetRollRad()), 15.0f, 1.0f);
  EXPECT_NEAR(RadToDeg(est.GetPitchRad()), 0.0f, 1.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Разгон НА наклоне: реальный тангаж + линейное ускорение — тангаж остаётся
// верным, если a_lin_long_g верно оценивает продольное ускорение.
// ═══════════════════════════════════════════════════════════════════════════

TEST(TiltEstimatorTest, TiltedForwardAccel_PitchStaysAccurate) {
  TiltEstimator est;
  ImuData imu{};
  imu.gx = imu.gy = imu.gz = 0.0f;

  const float pitch_true = DegToRad(20.0f);
  const float a_lin_g = 0.2f;
  // Удельная сила = проекция гравитации на наклоне + линейное ускорение
  imu.ax = -std::sin(pitch_true) + a_lin_g;
  imu.ay = 0.0f;
  imu.az = std::cos(pitch_true);

  for (int i = 0; i < 5000; ++i) {
    est.Update(imu, a_lin_g, kDt);
  }

  EXPECT_NEAR(RadToDeg(est.GetPitchRad()), 20.0f, 1.5f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Удар/выброс: |a| далеко от 1g → гейт закрыт, коррекция не применяется,
// тангаж не «прыгает» на мусорный accel-угол.
// ═══════════════════════════════════════════════════════════════════════════

TEST(TiltEstimatorTest, AccelSpike_GateRejectsCorrection) {
  TiltEstimator est;
  ImuData imu{};
  imu.gx = imu.gy = imu.gz = 0.0f;
  imu.ax = 0.0f;
  imu.ay = 0.0f;
  imu.az = 1.0f;

  // Дать coasted pitch устояться на 0 через несколько тиков без выброса.
  for (int i = 0; i < 10; ++i) {
    est.Update(imu, 0.0f, kDt);
  }
  ASSERT_NEAR(RadToDeg(est.GetPitchRad()), 0.0f, 0.1f);

  // Удар: 4g по X — если бы гейта не было, atan2(-4,1) дал бы ~-76°.
  imu.ax = 4.0f;
  imu.az = 1.0f;
  for (int i = 0; i < 50; ++i) {  // 100 мс удара
    est.Update(imu, 0.0f, kDt);
  }

  EXPECT_NEAR(RadToDeg(est.GetPitchRad()), 0.0f, 1.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Reset
// ═══════════════════════════════════════════════════════════════════════════

TEST(TiltEstimatorTest, Reset_RestoresZero) {
  TiltEstimator est;
  ImuData imu{};
  imu.gy = 50.0f;  // dps
  for (int i = 0; i < 100; ++i) {
    est.Update(imu, 0.0f, kDt);
  }
  ASSERT_NE(est.GetPitchRad(), 0.0f);

  est.Reset();
  EXPECT_FLOAT_EQ(est.GetPitchRad(), 0.0f);
  EXPECT_FLOAT_EQ(est.GetRollRad(), 0.0f);
}
