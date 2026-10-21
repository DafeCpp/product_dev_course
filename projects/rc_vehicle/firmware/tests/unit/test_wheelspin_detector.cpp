#include <gtest/gtest.h>

#include "wheelspin_detector.hpp"

using rc_vehicle::WheelspinDetector;

namespace {

constexpr float kDt = 0.002f;
constexpr float kG = 9.80665f;

// Разгон 1 с: колёса с ускорением wheel_accel, корпус с body_accel [м/с²].
WheelspinDetector Launch(float wheel_accel, float body_accel) {
  WheelspinDetector d;
  float wheel = 0.0f;
  for (int i = 0; i < 500; ++i) {
    wheel += wheel_accel * kDt;
    d.Update(wheel, wheel + 1.0f, body_accel / kG, kDt);
  }
  return d;
}

}  // namespace

TEST(WheelspinDetectorTest, NoSlipWhenBodyFollowsWheels) {
  const WheelspinDetector d = Launch(2.0f, 2.0f);
  EXPECT_NEAR(d.GetGroundSpeedMs(), 2.0f, 0.01f);
  EXPECT_NEAR(d.GetSlipRatio(), 0.0f, 0.01f);
}

TEST(WheelspinDetectorTest, BodyLaggingWheelsIsSlip) {
  // Лёд: колёса набирают 2 м/с, корпус — только 1 м/с²·t.
  const WheelspinDetector d = Launch(2.0f, 1.0f);
  EXPECT_GT(d.GetSlipRatio(), 0.3f);
  EXPECT_TRUE(d.IsDriving());
  EXPECT_LT(d.GetGroundSpeedMs(), 1.5f);
}

TEST(WheelspinDetectorTest, AccelBiasIsBoundedByLeak) {
  // Смещение акселерометра 0.02 g на разгоне: интеграл не уходит дальше
  // bias·g·τ ≈ 0.4 м/с, и при колёсах 2 м/с доля остаётся ниже 0.25.
  WheelspinDetector d;
  d.Reset(2.0f);
  for (int i = 0; i < 10000; ++i) d.Update(2.0f, 3.0f, -0.02f, kDt);
  EXPECT_LT(d.GetSlipRatio(), 0.25f);
}

TEST(WheelspinDetectorTest, SteadyDriveForgetsAccelBias) {
  // Код-ревью PR #402: на ровном ходу смещение не копится в долю, и
  // следующий разгон начинается с нуля, а не со старого дрейфа.
  WheelspinDetector d;
  d.Reset(2.0f);
  for (int i = 0; i < 10000; ++i) d.Update(2.0f, 2.0f, -0.02f, kDt);
  EXPECT_FLOAT_EQ(d.GetSlipRatio(), 0.0f);
  EXPECT_FALSE(d.IsDriving());
  d.Update(2.0f, 3.0f, -0.02f, kDt);
  EXPECT_TRUE(d.IsDriving());
  EXPECT_LT(d.GetSlipRatio(), 0.01f);
}

TEST(WheelspinDetectorTest, NoSlipWhenStoppedOrReversing) {
  WheelspinDetector d;
  d.Update(0.0f, 0.0f, -0.1f, kDt);
  EXPECT_FLOAT_EQ(d.GetSlipRatio(), 0.0f);
  d.Update(-1.0f, -1.0f, 0.0f, kDt);
  EXPECT_FLOAT_EQ(d.GetSlipRatio(), 0.0f);
}

TEST(WheelspinDetectorTest, ResetStartsFromGivenSpeed) {
  WheelspinDetector d;
  d.Reset(1.5f);
  EXPECT_FLOAT_EQ(d.GetGroundSpeedMs(), 1.5f);
  EXPECT_FLOAT_EQ(d.GetSlipRatio(), 0.0f);
}
