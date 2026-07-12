#include <gtest/gtest.h>

#include "firmware_common/pid_controller.hpp"

namespace firmware_common {
namespace {

TEST(PidControllerTest, ProportionalOutputIsClamped) {
  PidController pid({.kp = 2.0f, .max_output = 1.0f});

  EXPECT_FLOAT_EQ(pid.Step(0.25f, 0.01f), 0.5f);
  EXPECT_FLOAT_EQ(pid.Step(1.0f, 0.01f), 1.0f);
  EXPECT_FLOAT_EQ(pid.Step(-1.0f, 0.01f), -1.0f);
}

TEST(PidControllerTest, IntegralAccumulatesAndClamps) {
  PidController pid({.ki = 2.0f, .max_integral = 0.5f, .max_output = 10.0f});

  EXPECT_FLOAT_EQ(pid.Step(1.0f, 0.1f), 0.2f);
  for (int i = 0; i < 10; ++i) {
    (void)pid.Step(1.0f, 0.1f);
  }

  EXPECT_FLOAT_EQ(pid.GetIntegral(), 0.5f);
  EXPECT_FLOAT_EQ(pid.Step(-20.0f, 0.1f), -1.0f);
  EXPECT_FLOAT_EQ(pid.GetIntegral(), -0.5f);
}

TEST(PidControllerTest, DerivativeIsZeroOnFirstStep) {
  PidController pid({.kd = 1.0f, .max_output = 100.0f});

  EXPECT_FLOAT_EQ(pid.Step(5.0f, 0.1f), 0.0f);
  EXPECT_FLOAT_EQ(pid.Step(3.0f, 0.1f), -20.0f);
}

TEST(PidControllerTest, InvalidDtDoesNotChangeState) {
  PidController pid({.ki = 1.0f, .max_integral = 10.0f, .max_output = 10.0f});

  EXPECT_FLOAT_EQ(pid.Step(5.0f, 0.0f), 0.0f);
  EXPECT_FLOAT_EQ(pid.Step(5.0f, -0.1f), 0.0f);
  EXPECT_FLOAT_EQ(pid.GetIntegral(), 0.0f);
}

TEST(PidControllerTest, SetIntegralClampsAndResetClearsState) {
  PidController pid({.ki = 1.0f, .max_integral = 1.0f, .max_output = 10.0f});

  pid.SetIntegral(5.0f);
  EXPECT_FLOAT_EQ(pid.GetIntegral(), 1.0f);

  pid.Reset();
  EXPECT_FLOAT_EQ(pid.GetIntegral(), 0.0f);
}

}  // namespace
}  // namespace firmware_common
