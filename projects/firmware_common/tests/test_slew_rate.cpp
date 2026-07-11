#include <gtest/gtest.h>

#include "firmware_common/slew_rate.hpp"

namespace firmware_common {
namespace {

TEST(SlewRateTest, ReachesTargetInsideAllowedChange) {
  EXPECT_FLOAT_EQ(ApplySlewRate(0.5f, 0.4f, 2.0f, 0.1f), 0.5f);
}

TEST(SlewRateTest, LimitsPositiveAndNegativeChanges) {
  EXPECT_FLOAT_EQ(ApplySlewRate(1.0f, 0.0f, 2.0f, 0.1f), 0.2f);
  EXPECT_FLOAT_EQ(ApplySlewRate(-1.0f, 0.0f, 2.0f, 0.1f), -0.2f);
}

TEST(SlewRateTest, ZeroDtKeepsCurrentValue) {
  EXPECT_FLOAT_EQ(ApplySlewRate(1.0f, 0.25f, 2.0f, 0.0f), 0.25f);
}

TEST(SlewRateTest, ZeroRateKeepsCurrentValue) {
  EXPECT_FLOAT_EQ(ApplySlewRate(1.0f, 0.25f, 0.0f, 1.0f), 0.25f);
}

TEST(SlewRateTest, RepeatedStepsConvergeToTarget) {
  float current = 0.0f;
  for (int i = 0; i < 5; ++i) {
    current = ApplySlewRate(1.0f, current, 2.0f, 0.1f);
  }

  EXPECT_FLOAT_EQ(current, 1.0f);
}

}  // namespace
}  // namespace firmware_common
