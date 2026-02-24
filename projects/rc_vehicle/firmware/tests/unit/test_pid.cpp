#include <gtest/gtest.h>

#include <cmath>

#include "pid_controller.hpp"

using rc_vehicle::PidController;

// ═══════════════════════════════════════════════════════════════════════════
// Initial state
// ═══════════════════════════════════════════════════════════════════════════

TEST(PidTest, InitialIntegralIsZero) {
  PidController pid;
  EXPECT_FLOAT_EQ(pid.GetIntegral(), 0.0f);
}

TEST(PidTest, DefaultGainsAreZero) {
  PidController pid;
  const auto& g = pid.GetGains();
  EXPECT_FLOAT_EQ(g.kp, 0.0f);
  EXPECT_FLOAT_EQ(g.ki, 0.0f);
  EXPECT_FLOAT_EQ(g.kd, 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Zero gains
// ═══════════════════════════════════════════════════════════════════════════

TEST(PidTest, ZeroGainsAlwaysReturnZero) {
  PidController pid;
  EXPECT_FLOAT_EQ(pid.Step(10.0f, 0.002f), 0.0f);
  EXPECT_FLOAT_EQ(pid.Step(-5.0f, 0.002f), 0.0f);
  EXPECT_FLOAT_EQ(pid.Step(0.0f, 0.002f), 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Proportional-only
// ═══════════════════════════════════════════════════════════════════════════

TEST(PidTest, POnlyOutputIsKpTimesError) {
  PidController pid({.kp = 2.0f, .ki = 0.0f, .kd = 0.0f,
                     .max_integral = 1.0f, .max_output = 100.0f});
  const float error = 5.0f;
  EXPECT_FLOAT_EQ(pid.Step(error, 0.002f), 2.0f * error);
}

TEST(PidTest, POnlyNegativeError) {
  PidController pid({.kp = 3.0f, .ki = 0.0f, .kd = 0.0f,
                     .max_integral = 1.0f, .max_output = 100.0f});
  EXPECT_FLOAT_EQ(pid.Step(-4.0f, 0.002f), -12.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Integral-only
// ═══════════════════════════════════════════════════════════════════════════

TEST(PidTest, IOnlyAccumulates) {
  PidController pid({.kp = 0.0f, .ki = 1.0f, .kd = 0.0f,
                     .max_integral = 100.0f, .max_output = 100.0f});
  const float dt = 0.002f;
  const float error = 10.0f;

  pid.Step(error, dt);  // integral = 10 * 0.002 = 0.02
  EXPECT_NEAR(pid.GetIntegral(), error * dt, 1e-5f);

  pid.Step(error, dt);  // integral = 0.04
  EXPECT_NEAR(pid.GetIntegral(), 2.0f * error * dt, 1e-5f);
}

TEST(PidTest, IOnlyOutputEqualsKiTimesIntegral) {
  PidController pid({.kp = 0.0f, .ki = 5.0f, .kd = 0.0f,
                     .max_integral = 100.0f, .max_output = 100.0f});
  const float dt = 0.01f;
  const float error = 2.0f;

  const float out = pid.Step(error, dt);
  const float expected_integral = error * dt;
  EXPECT_NEAR(out, 5.0f * expected_integral, 1e-5f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Anti-windup
// ═══════════════════════════════════════════════════════════════════════════

TEST(PidTest, AntiWindupClampsIntegral) {
  PidController pid({.kp = 0.0f, .ki = 1.0f, .kd = 0.0f,
                     .max_integral = 0.1f, .max_output = 100.0f});
  // Drive integral far past the limit
  for (int i = 0; i < 1000; ++i) {
    pid.Step(100.0f, 0.01f);
  }
  EXPECT_LE(pid.GetIntegral(), 0.1f + 1e-5f);
}

TEST(PidTest, AntiWindupClampsIntegralNegative) {
  PidController pid({.kp = 0.0f, .ki = 1.0f, .kd = 0.0f,
                     .max_integral = 0.1f, .max_output = 100.0f});
  for (int i = 0; i < 1000; ++i) {
    pid.Step(-100.0f, 0.01f);
  }
  EXPECT_GE(pid.GetIntegral(), -0.1f - 1e-5f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Derivative
// ═══════════════════════════════════════════════════════════════════════════

TEST(PidTest, DOnlyFirstStepIsZero) {
  PidController pid({.kp = 0.0f, .ki = 0.0f, .kd = 10.0f,
                     .max_integral = 1.0f, .max_output = 100.0f});
  // First step: no previous error, derivative = 0
  EXPECT_FLOAT_EQ(pid.Step(5.0f, 0.002f), 0.0f);
}

TEST(PidTest, DOnlySecondStepIsCorrect) {
  PidController pid({.kp = 0.0f, .ki = 0.0f, .kd = 1.0f,
                     .max_integral = 1.0f, .max_output = 1000.0f});
  const float dt = 0.01f;
  pid.Step(2.0f, dt);                    // first step, D = 0
  const float out = pid.Step(4.0f, dt);  // de = 4 - 2 = 2, de/dt = 200
  EXPECT_NEAR(out, (4.0f - 2.0f) / dt, 1e-4f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Reset
// ═══════════════════════════════════════════════════════════════════════════

TEST(PidTest, ResetClearsIntegral) {
  PidController pid({.kp = 0.0f, .ki = 1.0f, .kd = 0.0f,
                     .max_integral = 100.0f, .max_output = 100.0f});
  pid.Step(10.0f, 0.01f);
  EXPECT_GT(pid.GetIntegral(), 0.0f);

  pid.Reset();
  EXPECT_FLOAT_EQ(pid.GetIntegral(), 0.0f);
}

TEST(PidTest, ResetMakesNextStepLikeFirstStep) {
  PidController pid({.kp = 0.0f, .ki = 0.0f, .kd = 1.0f,
                     .max_integral = 1.0f, .max_output = 100.0f});
  pid.Step(2.0f, 0.01f);
  pid.Step(4.0f, 0.01f);  // now first_step_ = false

  pid.Reset();
  // After reset, next step should have D = 0 (like first step)
  EXPECT_FLOAT_EQ(pid.Step(10.0f, 0.01f), 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Output clamping
// ═══════════════════════════════════════════════════════════════════════════

TEST(PidTest, OutputClampedPositive) {
  PidController pid({.kp = 100.0f, .ki = 0.0f, .kd = 0.0f,
                     .max_integral = 1.0f, .max_output = 0.3f});
  const float out = pid.Step(10.0f, 0.002f);
  EXPECT_LE(out, 0.3f + 1e-6f);
}

TEST(PidTest, OutputClampedNegative) {
  PidController pid({.kp = 100.0f, .ki = 0.0f, .kd = 0.0f,
                     .max_integral = 1.0f, .max_output = 0.3f});
  const float out = pid.Step(-10.0f, 0.002f);
  EXPECT_GE(out, -0.3f - 1e-6f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Zero / negative dt
// ═══════════════════════════════════════════════════════════════════════════

TEST(PidTest, ZeroDtReturnsZero) {
  PidController pid({.kp = 1.0f, .ki = 1.0f, .kd = 1.0f,
                     .max_integral = 1.0f, .max_output = 100.0f});
  EXPECT_FLOAT_EQ(pid.Step(5.0f, 0.0f), 0.0f);
}

TEST(PidTest, ZeroDtDoesNotChangeIntegral) {
  PidController pid({.kp = 0.0f, .ki = 1.0f, .kd = 0.0f,
                     .max_integral = 100.0f, .max_output = 100.0f});
  pid.Step(5.0f, 0.0f);
  EXPECT_FLOAT_EQ(pid.GetIntegral(), 0.0f);
}

TEST(PidTest, NegativeDtReturnsZero) {
  PidController pid({.kp = 1.0f, .ki = 0.0f, .kd = 0.0f,
                     .max_integral = 1.0f, .max_output = 100.0f});
  EXPECT_FLOAT_EQ(pid.Step(5.0f, -0.001f), 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Combined PID
// ═══════════════════════════════════════════════════════════════════════════

TEST(PidTest, CombinedPidFirstStep) {
  // On the first step: D=0, so output = Kp*e + Ki*(e*dt)
  PidController pid({.kp = 1.0f, .ki = 2.0f, .kd = 0.5f,
                     .max_integral = 100.0f, .max_output = 100.0f});
  const float error = 4.0f;
  const float dt = 0.01f;
  const float expected = 1.0f * error + 2.0f * (error * dt) + 0.0f;
  EXPECT_NEAR(pid.Step(error, dt), expected, 1e-5f);
}

TEST(PidTest, SetGainsUpdatesGains) {
  PidController pid;
  pid.SetGains({.kp = 2.0f, .ki = 3.0f, .kd = 0.1f,
                .max_integral = 5.0f, .max_output = 1.0f});
  const auto& g = pid.GetGains();
  EXPECT_FLOAT_EQ(g.kp, 2.0f);
  EXPECT_FLOAT_EQ(g.ki, 3.0f);
  EXPECT_FLOAT_EQ(g.kd, 0.1f);
  EXPECT_FLOAT_EQ(g.max_integral, 5.0f);
  EXPECT_FLOAT_EQ(g.max_output, 1.0f);
}
