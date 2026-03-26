#include <gtest/gtest.h>

#include "telemetry_manager.hpp"

using rc_vehicle::TelemetryManager;

// ═══════════════════════════════════════════════════════════════════════════
// Init
// ═══════════════════════════════════════════════════════════════════════════

TEST(TelemetryManagerTest, Init_Success) {
  TelemetryManager mgr;
  EXPECT_TRUE(mgr.Init(100));

  size_t count = 0, cap = 0;
  mgr.GetLogInfo(count, cap);
  EXPECT_EQ(count, 0u);
  EXPECT_EQ(cap, 100u);
}

TEST(TelemetryManagerTest, Init_ZeroCapacity_Fails) {
  TelemetryManager mgr;
  EXPECT_FALSE(mgr.Init(0));
}

// ═══════════════════════════════════════════════════════════════════════════
// Push and GetLogFrame
// ═══════════════════════════════════════════════════════════════════════════

TEST(TelemetryManagerTest, Push_IncreasesCount) {
  TelemetryManager mgr;
  ASSERT_TRUE(mgr.Init(10));

  TelemetryLogFrame frame{};
  frame.ts_ms = 1000;
  mgr.Push(frame);

  size_t count = 0, cap = 0;
  mgr.GetLogInfo(count, cap);
  EXPECT_EQ(count, 1u);
}

TEST(TelemetryManagerTest, GetLogFrame_ReturnsCorrectData) {
  TelemetryManager mgr;
  ASSERT_TRUE(mgr.Init(10));

  TelemetryLogFrame frame{};
  frame.ts_ms = 42;
  frame.throttle = 0.5f;
  mgr.Push(frame);

  TelemetryLogFrame out{};
  ASSERT_TRUE(mgr.GetLogFrame(0, out));
  EXPECT_EQ(out.ts_ms, 42u);
  EXPECT_FLOAT_EQ(out.throttle, 0.5f);
}

TEST(TelemetryManagerTest, GetLogFrame_OutOfRange_ReturnsFalse) {
  TelemetryManager mgr;
  ASSERT_TRUE(mgr.Init(10));

  TelemetryLogFrame out{};
  EXPECT_FALSE(mgr.GetLogFrame(0, out));
}

// ═══════════════════════════════════════════════════════════════════════════
// Clear
// ═══════════════════════════════════════════════════════════════════════════

TEST(TelemetryManagerTest, Clear_ResetsCount) {
  TelemetryManager mgr;
  ASSERT_TRUE(mgr.Init(10));

  TelemetryLogFrame frame{};
  mgr.Push(frame);
  mgr.Push(frame);

  mgr.Clear();

  size_t count = 0, cap = 0;
  mgr.GetLogInfo(count, cap);
  EXPECT_EQ(count, 0u);
  EXPECT_EQ(cap, 10u);
}

// ═══════════════════════════════════════════════════════════════════════════
// LastLogTime
// ═══════════════════════════════════════════════════════════════════════════

TEST(TelemetryManagerTest, LastLogTime_DefaultsToZero) {
  TelemetryManager mgr;
  EXPECT_EQ(mgr.GetLastLogTime(), 0u);
}

TEST(TelemetryManagerTest, SetLastLogTime_UpdatesValue) {
  TelemetryManager mgr;
  mgr.SetLastLogTime(12345);
  EXPECT_EQ(mgr.GetLastLogTime(), 12345u);
}

TEST(TelemetryManagerTest, ResetLastLogTime_SetsToZero) {
  TelemetryManager mgr;
  mgr.SetLastLogTime(999);
  mgr.ResetLastLogTime();
  EXPECT_EQ(mgr.GetLastLogTime(), 0u);
}

// ═══════════════════════════════════════════════════════════════════════════
// Ring buffer wrapping
// ═══════════════════════════════════════════════════════════════════════════

TEST(TelemetryManagerTest, Push_WrapsAround) {
  TelemetryManager mgr;
  const size_t cap = 3;
  ASSERT_TRUE(mgr.Init(cap));

  for (uint32_t i = 0; i < 5; ++i) {
    TelemetryLogFrame frame{};
    frame.ts_ms = i + 1;
    mgr.Push(frame);
  }

  size_t count = 0, cap_out = 0;
  mgr.GetLogInfo(count, cap_out);
  EXPECT_EQ(count, cap);

  // Oldest should be frame 3 (frames 1,2 overwritten)
  TelemetryLogFrame out{};
  ASSERT_TRUE(mgr.GetLogFrame(0, out));
  EXPECT_EQ(out.ts_ms, 3u);

  ASSERT_TRUE(mgr.GetLogFrame(2, out));
  EXPECT_EQ(out.ts_ms, 5u);
}
