#include <gtest/gtest.h>

#include "ramp_generator.hpp"

namespace bench {
namespace {

TEST(RampGenerator, OneBeforeStart) {
  RampGenerator r;
  EXPECT_FLOAT_EQ(r.Scale(123), 1.0f);
  EXPECT_FALSE(r.Done(123));
}

TEST(RampGenerator, LinearDescentAndEndpoints) {
  RampGenerator r;
  r.Start(1000, 400);
  EXPECT_FLOAT_EQ(r.Scale(1000), 1.0f);
  EXPECT_FLOAT_EQ(r.Scale(1200), 0.5f);
  EXPECT_FLOAT_EQ(r.Scale(1400), 0.0f);
  EXPECT_FLOAT_EQ(r.Scale(2000), 0.0f);
  EXPECT_TRUE(r.Done(1400));
  EXPECT_FALSE(r.Done(1398));
}

TEST(RampGenerator, ZeroDurationIsImmediatelyDone) {
  RampGenerator r;
  r.Start(100, 0);
  EXPECT_FLOAT_EQ(r.Scale(100), 0.0f);
  EXPECT_TRUE(r.Done(100));
}

TEST(RampGenerator, ResetRestoresIdle) {
  RampGenerator r;
  r.Start(0, 100);
  r.Reset();
  EXPECT_FLOAT_EQ(r.Scale(50), 1.0f);
}

}  // namespace
}  // namespace bench
