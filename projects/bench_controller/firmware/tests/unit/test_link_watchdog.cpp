#include <gtest/gtest.h>

#include "link_watchdog.hpp"

namespace bench {
namespace {

LinkWatchdog MakeWatchdog() {
  return LinkWatchdog({.grace_ms = 100, .ramp_ms = 400});
}

TEST(LinkWatchdog, StaysRunningWhileAlive) {
  auto w = MakeWatchdog();
  for (uint32_t t = 0; t < 10'000; t += 2) {
    EXPECT_EQ(w.Update(t, true), LinkState::kRunning);
    EXPECT_FLOAT_EQ(w.RampScale(t), 1.0f);
  }
}

TEST(LinkWatchdog, LossEntersGraceThenRampThenHold) {
  auto w = MakeWatchdog();
  EXPECT_EQ(w.Update(0, true), LinkState::kRunning);
  EXPECT_EQ(w.Update(2, false), LinkState::kGracePeriod);
  EXPECT_EQ(w.Update(100, false), LinkState::kGracePeriod);
  EXPECT_EQ(w.Update(102, false), LinkState::kRampDown);
  EXPECT_EQ(w.Update(300, false), LinkState::kRampDown);
  EXPECT_EQ(w.Update(502, false), LinkState::kSafeHold);
}

TEST(LinkWatchdog, RecoveryFromGraceReturnsToRunning) {
  auto w = MakeWatchdog();
  (void)w.Update(0, true);
  (void)w.Update(2, false);
  EXPECT_EQ(w.Update(50, true), LinkState::kRunning);
  EXPECT_FLOAT_EQ(w.RampScale(50), 1.0f);
}

TEST(LinkWatchdog, RecoveryDuringRampDoesNotAbortRamp) {
  auto w = MakeWatchdog();
  (void)w.Update(0, true);
  (void)w.Update(2, false);
  (void)w.Update(102, false);  // → RampDown
  // Связь вернулась — разгрузку всё равно доводим до конца.
  EXPECT_EQ(w.Update(200, true), LinkState::kRampDown);
  EXPECT_EQ(w.Update(502, true), LinkState::kSafeHold);
}

TEST(LinkWatchdog, RampScaleDecreasesMonotonically) {
  auto w = MakeWatchdog();
  (void)w.Update(0, true);
  (void)w.Update(2, false);
  (void)w.Update(102, false);  // старт рампы при t = 102
  float prev = 1.0f;
  for (uint32_t t = 104; t <= 502; t += 2) {
    (void)w.Update(t, false);
    const float s = w.RampScale(t);
    EXPECT_LE(s, prev);
    prev = s;
  }
  EXPECT_FLOAT_EQ(prev, 0.0f);
}

TEST(LinkWatchdog, SafeHoldRequiresExplicitReset) {
  auto w = MakeWatchdog();
  (void)w.Update(0, true);
  (void)w.Update(2, false);
  (void)w.Update(102, false);
  (void)w.Update(502, false);
  ASSERT_EQ(w.State(), LinkState::kSafeHold);
  EXPECT_EQ(w.Update(600, true), LinkState::kSafeHold);
  w.Reset();
  EXPECT_EQ(w.Update(602, true), LinkState::kRunning);
}

}  // namespace
}  // namespace bench
