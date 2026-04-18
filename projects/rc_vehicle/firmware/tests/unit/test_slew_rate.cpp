#include <gtest/gtest.h>

#include "slew_rate.hpp"

// ══════════════════════════════════════════════════════════════════════════════
// ApplySlewRate: плавное нарастание/спад управляющего сигнала
// ══════════════════════════════════════════════════════════════════════════════

TEST(SlewRate, ReachesTarget_WhenDiffSmall) {
  // target close to current — should reach exactly
  float result = ApplySlewRate(0.5f, 0.49f, 10.0f, 20);
  EXPECT_FLOAT_EQ(result, 0.5f);
}

TEST(SlewRate, LimitsPositiveChange) {
  // max_change_per_sec=1.0, dt=100ms → max_change=0.1
  float result = ApplySlewRate(1.0f, 0.0f, 1.0f, 100);
  EXPECT_FLOAT_EQ(result, 0.1f);
}

TEST(SlewRate, LimitsNegativeChange) {
  // max_change_per_sec=1.0, dt=100ms → max_change=0.1
  float result = ApplySlewRate(-1.0f, 0.0f, 1.0f, 100);
  EXPECT_FLOAT_EQ(result, -0.1f);
}

TEST(SlewRate, NoChange_WhenDtZero) {
  // dt=0 → max_change=0 → stays at current
  float result = ApplySlewRate(1.0f, 0.0f, 1.0f, 0);
  EXPECT_FLOAT_EQ(result, 0.0f);
}

TEST(SlewRate, NoChange_WhenMaxRateZero) {
  // max_change_per_sec=0 → always stays at current
  float result = ApplySlewRate(1.0f, 0.5f, 0.0f, 100);
  EXPECT_FLOAT_EQ(result, 0.5f);
}

TEST(SlewRate, ConvergesOverTime) {
  // Multiple steps: 0.0 → 1.0 at 2.0/sec, dt=100ms → +0.2 per step
  float val = 0.0f;
  for (int i = 0; i < 5; ++i) {
    val = ApplySlewRate(1.0f, val, 2.0f, 100);
  }
  EXPECT_FLOAT_EQ(val, 1.0f);  // 5 * 0.2 = 1.0
}

TEST(SlewRate, WorksWithNegativeValues) {
  // current=-0.5, target=0.5 at 1.0/sec, dt=200ms → +0.2
  float result = ApplySlewRate(0.5f, -0.5f, 1.0f, 200);
  EXPECT_FLOAT_EQ(result, -0.3f);
}

TEST(SlewRate, IdenticalTargetAndCurrent) {
  float result = ApplySlewRate(0.5f, 0.5f, 1.0f, 100);
  EXPECT_FLOAT_EQ(result, 0.5f);
}

TEST(SlewRate, LargeDt_LargeMaxChange) {
  // dt=1000ms, rate=10.0/sec → max_change=10.0 → reaches target
  float result = ApplySlewRate(0.5f, 0.0f, 10.0f, 1000);
  EXPECT_FLOAT_EQ(result, 0.5f);
}

TEST(SlewRate, SmallDt_SmallStep) {
  // dt=2ms (500Hz control loop), rate=0.5/sec → max_change=0.001
  float result = ApplySlewRate(1.0f, 0.0f, 0.5f, 2);
  EXPECT_FLOAT_EQ(result, 0.001f);
}
