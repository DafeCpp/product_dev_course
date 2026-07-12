#include <gtest/gtest.h>

#include <firmware_common/pid_controller.hpp>

namespace bench {
namespace {

using firmware_common::PidController;

TEST(PidController, ZeroDtReturnsZeroAndKeepsState) {
  PidController pid(
      {.kp = 1.0f, .ki = 1.0f, .max_integral = 10.0f, .max_output = 10.0f});
  EXPECT_EQ(pid.Step(5.0f, 0.0f), 0.0f);
  EXPECT_EQ(pid.GetIntegral(), 0.0f);
}

TEST(PidController, ProportionalOnly) {
  PidController pid({.kp = 2.0f, .max_output = 10.0f});
  EXPECT_FLOAT_EQ(pid.Step(3.0f, 0.01f), 6.0f);
}

TEST(PidController, IntegralAccumulatesAndClamps) {
  PidController pid({.ki = 1.0f, .max_integral = 0.5f, .max_output = 10.0f});
  for (int i = 0; i < 100; ++i) {
    (void)pid.Step(1.0f, 0.1f);
  }
  EXPECT_FLOAT_EQ(pid.GetIntegral(), 0.5f);
}

TEST(PidController, DerivativeZeroOnFirstStep) {
  PidController pid({.kd = 1.0f, .max_output = 100.0f});
  EXPECT_FLOAT_EQ(pid.Step(5.0f, 0.1f), 0.0f);
  // Второй шаг: de = -2, dt = 0.1 → d = -20
  EXPECT_FLOAT_EQ(pid.Step(3.0f, 0.1f), -20.0f);
}

TEST(PidController, OutputClamped) {
  PidController pid({.kp = 100.0f, .max_output = 1.0f});
  EXPECT_FLOAT_EQ(pid.Step(10.0f, 0.01f), 1.0f);
  EXPECT_FLOAT_EQ(pid.Step(-10.0f, 0.01f), -1.0f);
}

TEST(PidController, SetIntegralPreloadsFirstOutput) {
  // Bumpless: при e = 0 первый выход должен равняться ki · I.
  PidController pid({.ki = 2.0f, .max_integral = 10.0f, .max_output = 10.0f});
  pid.SetIntegral(0.25f);
  const float out = pid.Step(0.0f, 0.002f);
  EXPECT_NEAR(out, 0.5f, 1e-4f);
}

TEST(PidController, SetIntegralClampsToMaxIntegral) {
  PidController pid({.ki = 1.0f, .max_integral = 1.0f, .max_output = 10.0f});
  pid.SetIntegral(5.0f);
  EXPECT_FLOAT_EQ(pid.GetIntegral(), 1.0f);
}

TEST(PidController, ResetClearsState) {
  PidController pid({.kd = 1.0f, .max_output = 100.0f});
  (void)pid.Step(1.0f, 0.1f);
  (void)pid.Step(2.0f, 0.1f);  // теперь есть история производной
  pid.Reset();
  EXPECT_EQ(pid.GetIntegral(), 0.0f);
  // После Reset первый шаг снова без D-составляющей.
  EXPECT_FLOAT_EQ(pid.Step(5.0f, 0.1f), 0.0f);
}

}  // namespace
}  // namespace bench
