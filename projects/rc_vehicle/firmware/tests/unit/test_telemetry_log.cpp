#include <gtest/gtest.h>

#include "telemetry_log.hpp"

// ═══════════════════════════════════════════════════════════════════════════
// Init
// ═══════════════════════════════════════════════════════════════════════════

TEST(TelemetryLogTest, Init_ReturnsTrue_CapacitySet) {
  TelemetryLog log;
  EXPECT_TRUE(log.Init(10));
  EXPECT_EQ(log.Capacity(), 10u);
  EXPECT_EQ(log.Count(), 0u);
}

TEST(TelemetryLogTest, Init_ZeroCapacity_ReturnsFalse) {
  TelemetryLog log;
  EXPECT_FALSE(log.Init(0));
  EXPECT_EQ(log.Capacity(), 0u);
}

// ═══════════════════════════════════════════════════════════════════════════
// Push
// ═══════════════════════════════════════════════════════════════════════════

TEST(TelemetryLogTest, Push_IncreasesCount) {
  TelemetryLog log;
  ASSERT_TRUE(log.Init(10));

  TelemetryLogFrame frame;
  frame.ts_ms = 1000;
  log.Push(frame);
  EXPECT_EQ(log.Count(), 1u);

  log.Push(frame);
  EXPECT_EQ(log.Count(), 2u);
}

TEST(TelemetryLogTest, Push_CountCapsAtCapacity) {
  TelemetryLog log;
  const size_t cap = 5;
  ASSERT_TRUE(log.Init(cap));

  TelemetryLogFrame frame;
  for (size_t i = 0; i < cap + 3; ++i) {
    frame.ts_ms = static_cast<uint32_t>(i);
    log.Push(frame);
  }

  // Count должен остаться cap (кольцевой буфер)
  EXPECT_EQ(log.Count(), cap);
}

// ═══════════════════════════════════════════════════════════════════════════
// GetFrame
// ═══════════════════════════════════════════════════════════════════════════

TEST(TelemetryLogTest, GetFrame_ReturnsOldestFirst) {
  TelemetryLog log;
  ASSERT_TRUE(log.Init(10));

  for (uint32_t i = 0; i < 3; ++i) {
    TelemetryLogFrame frame;
    frame.ts_ms = i + 1;
    log.Push(frame);
  }

  TelemetryLogFrame out;
  ASSERT_TRUE(log.GetFrame(0, out));
  EXPECT_EQ(out.ts_ms, 1u);  // oldest

  ASSERT_TRUE(log.GetFrame(1, out));
  EXPECT_EQ(out.ts_ms, 2u);

  ASSERT_TRUE(log.GetFrame(2, out));
  EXPECT_EQ(out.ts_ms, 3u);  // newest
}

TEST(TelemetryLogTest, GetFrame_AfterWrap_OldestFirst) {
  TelemetryLog log;
  const size_t cap = 4;
  ASSERT_TRUE(log.Init(cap));

  // Добавляем cap+2 кадра → кольцо перезаписывает первые 2
  for (uint32_t i = 0; i < cap + 2; ++i) {
    TelemetryLogFrame frame;
    frame.ts_ms = i + 1;
    log.Push(frame);
  }

  // Теперь oldest = кадр с ts_ms == 3 (i=2), newest == cap+2
  EXPECT_EQ(log.Count(), cap);

  TelemetryLogFrame out;
  ASSERT_TRUE(log.GetFrame(0, out));
  EXPECT_EQ(out.ts_ms, 3u);

  ASSERT_TRUE(log.GetFrame(cap - 1, out));
  EXPECT_EQ(out.ts_ms, static_cast<uint32_t>(cap + 2));
}

TEST(TelemetryLogTest, GetFrame_OutOfRange_ReturnsFalse) {
  TelemetryLog log;
  ASSERT_TRUE(log.Init(5));

  TelemetryLogFrame frame{};
  log.Push(frame);  // Count = 1

  TelemetryLogFrame out;
  EXPECT_FALSE(log.GetFrame(1, out));   // idx == Count, invalid
  EXPECT_FALSE(log.GetFrame(10, out));  // far out of range
}

TEST(TelemetryLogTest, GetFrame_EmptyLog_ReturnsFalse) {
  TelemetryLog log;
  ASSERT_TRUE(log.Init(5));

  TelemetryLogFrame out;
  EXPECT_FALSE(log.GetFrame(0, out));
}

// ═══════════════════════════════════════════════════════════════════════════
// Clear
// ═══════════════════════════════════════════════════════════════════════════

TEST(TelemetryLogTest, Clear_ResetsCount) {
  TelemetryLog log;
  ASSERT_TRUE(log.Init(5));

  TelemetryLogFrame frame{};
  log.Push(frame);
  log.Push(frame);
  EXPECT_EQ(log.Count(), 2u);

  log.Clear();
  EXPECT_EQ(log.Count(), 0u);
}

TEST(TelemetryLogTest, Clear_ThenPush_Works) {
  TelemetryLog log;
  ASSERT_TRUE(log.Init(3));

  TelemetryLogFrame frame;
  for (int i = 0; i < 3; ++i) {
    frame.ts_ms = i + 1;
    log.Push(frame);
  }
  log.Clear();
  EXPECT_EQ(log.Count(), 0u);

  frame.ts_ms = 42;
  log.Push(frame);
  EXPECT_EQ(log.Count(), 1u);

  TelemetryLogFrame out;
  ASSERT_TRUE(log.GetFrame(0, out));
  EXPECT_EQ(out.ts_ms, 42u);
}
