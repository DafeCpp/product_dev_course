#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include "grip_loss_detector.hpp"

using rc_vehicle::GripLossDetector;

namespace {

constexpr float kDt = 0.002f;

// Установившийся yaw rate при сцеплении для руля steering на скорости vx.
float GripYawRate(float vx, float steering) {
  return vx * std::tan(steering * GripLossDetector::kMaxSteerRad) /
         GripLossDetector::kWheelbaseM;
}

GripLossDetector Drive(float yaw_rate, float vx, float steering, int ticks) {
  GripLossDetector d;
  for (int i = 0; i < ticks; ++i) d.Update(yaw_rate, vx, steering, kDt);
  return d;
}

}  // namespace

TEST(GripLossDetectorTest, YawRateFollowingSteeringIsGrip) {
  const float r = 0.97f * GripYawRate(2.0f, 0.7f);  // асфальт в SIL: −3 %
  const GripLossDetector d = Drive(r, 2.0f, 0.7f, 500);
  EXPECT_FALSE(d.IsGripLost());
  EXPECT_NEAR(d.GetReferenceYawRateRps(), GripYawRate(2.0f, 0.7f), 1e-4f);
}

TEST(GripLossDetectorTest, YawRateBelowSteeringIsGripLoss) {
  // Скользкое в SIL: 1.27 рад/с при эталоне 1.73.
  const GripLossDetector d =
      Drive(0.73f * GripYawRate(2.0f, 0.7f), 2.0f, 0.7f, 500);
  EXPECT_TRUE(d.IsGripLost());
}

TEST(GripLossDetectorTest, SpinBeyondSteeringIsGripLoss) {
  const GripLossDetector d = Drive(2.0f, 2.0f, 0.1f, 500);
  EXPECT_TRUE(d.IsGripLost());
}

TEST(GripLossDetectorTest, ShortDeviationIsNotGripLoss) {
  // Вход в поворот: yaw rate догоняет руль за десятки мс.
  GripLossDetector d;
  for (int i = 0; i < 50; ++i) d.Update(0.0f, 2.0f, 0.7f, kDt);  // 100 мс
  EXPECT_FALSE(d.IsGripLost());
}

TEST(GripLossDetectorTest, YawLaggingSteeringRampIsNotGripLoss) {
  // Асфальт в SIL: руль идёт к 0.7 со slew 3/с, yaw rate отстаёт на 50 мс.
  GripLossDetector d;
  constexpr int kLagTicks = 25;
  bool lost = false;
  for (int i = 0; i < 500; ++i) {
    const float steer = std::min(3.0f * kDt * i, 0.7f);
    const float lagged =
        std::min(3.0f * kDt * std::max(i - kLagTicks, 0), 0.7f);
    d.Update(GripYawRate(2.0f, lagged), 2.0f, steer, kDt);
    lost = lost || d.IsGripLost();
  }
  EXPECT_FALSE(lost);
}

TEST(GripLossDetectorTest, GripReturnsAfterYawRateRecovers) {
  GripLossDetector d = Drive(0.0f, 2.0f, 0.7f, 200);
  ASSERT_TRUE(d.IsGripLost());
  const float r = GripYawRate(2.0f, 0.7f);
  for (int i = 0; i < 50; ++i) d.Update(r, 2.0f, 0.7f, kDt);
  EXPECT_TRUE(d.IsGripLost());  // гистерезис по времени
  for (int i = 0; i < 50; ++i) d.Update(r, 2.0f, 0.7f, kDt);
  EXPECT_FALSE(d.IsGripLost());
}

TEST(GripLossDetectorTest, SlowOrReversingIsNotEvaluated) {
  GripLossDetector d = Drive(0.0f, 2.0f, 0.7f, 200);
  ASSERT_TRUE(d.IsGripLost());
  d.Update(0.0f, 0.3f, 0.7f, kDt);
  EXPECT_FALSE(d.IsGripLost());
  EXPECT_FALSE(Drive(1.0f, -1.0f, 0.7f, 500).IsGripLost());
}

TEST(GripLossDetectorTest, SteeringForYawRateInvertsReference) {
  GripLossDetector det;
  det.Update(0.0f, 2.0f, 0.6f, 0.002f);
  const float r_ref = det.GetReferenceYawRateRps();
  EXPECT_NEAR(GripLossDetector::SteeringForYawRate(r_ref, 2.0f), 0.6f, 1e-5f);
  EXPECT_NEAR(GripLossDetector::SteeringForYawRate(-r_ref, 2.0f), 0.6f, 1e-5f);
  EXPECT_FLOAT_EQ(GripLossDetector::SteeringForYawRate(100.0f, 2.0f), 1.0f);
  EXPECT_FLOAT_EQ(GripLossDetector::SteeringForYawRate(1.0f, 0.0f), 1.0f);
}
