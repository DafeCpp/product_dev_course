#include <gtest/gtest.h>

#include "telemetry_event_log.hpp"

using namespace rc_vehicle;

// ═══════════════════════════════════════════════════════════════════════════
// TelemetryEventLog — базовые операции
// ═══════════════════════════════════════════════════════════════════════════

TEST(TelemetryEventLogTest, InitiallyEmpty) {
  TelemetryEventLog log;
  EXPECT_EQ(log.Count(), 0u);
  EXPECT_EQ(log.Capacity(), TelemetryEventLog::kCapacity);
}

TEST(TelemetryEventLogTest, PushIncreasesCount) {
  TelemetryEventLog log;
  log.Push({1000, TelemetryEventType::TestStart, 1});
  EXPECT_EQ(log.Count(), 1u);
}

TEST(TelemetryEventLogTest, GetEvent_ReturnsCorrectData) {
  TelemetryEventLog log;
  TelemetryEvent evt{42, TelemetryEventType::TrimCalibStart, 0};
  log.Push(evt);

  TelemetryEvent out{};
  ASSERT_TRUE(log.GetEvent(0, out));
  EXPECT_EQ(out.ts_ms, 42u);
  EXPECT_EQ(out.type, TelemetryEventType::TrimCalibStart);
  EXPECT_EQ(out.param, 0u);
}

TEST(TelemetryEventLogTest, GetEvent_OutOfRange_ReturnsFalse) {
  TelemetryEventLog log;
  TelemetryEvent out{};
  EXPECT_FALSE(log.GetEvent(0, out));
}

TEST(TelemetryEventLogTest, Clear_ResetsCount) {
  TelemetryEventLog log;
  log.Push({1, TelemetryEventType::TestStart, 1});
  log.Push({2, TelemetryEventType::TestDone, 1});
  log.Clear();
  EXPECT_EQ(log.Count(), 0u);
}

// ═══════════════════════════════════════════════════════════════════════════
// Порядок элементов: oldest first
// ═══════════════════════════════════════════════════════════════════════════

TEST(TelemetryEventLogTest, OldestFirst) {
  TelemetryEventLog log;
  log.Push({10, TelemetryEventType::TestStart, 1});
  log.Push({20, TelemetryEventType::TestDone, 1});
  log.Push({30, TelemetryEventType::ImuCalibStart, 0});

  TelemetryEvent out{};
  ASSERT_TRUE(log.GetEvent(0, out));
  EXPECT_EQ(out.ts_ms, 10u);  // oldest

  ASSERT_TRUE(log.GetEvent(2, out));
  EXPECT_EQ(out.ts_ms, 30u);  // newest
}

// ═══════════════════════════════════════════════════════════════════════════
// Кольцевой буфер: вытеснение при переполнении
// ═══════════════════════════════════════════════════════════════════════════

TEST(TelemetryEventLogTest, WrapAround_EvictsOldest) {
  TelemetryEventLog log;
  const size_t cap = TelemetryEventLog::kCapacity;

  // Заполняем буфер полностью
  for (uint32_t i = 0; i < cap; ++i) {
    log.Push({i, TelemetryEventType::TestStart, 0});
  }
  EXPECT_EQ(log.Count(), cap);

  // Добавляем ещё одно событие — должно вытеснить самое старое (ts=0)
  log.Push({static_cast<uint32_t>(cap), TelemetryEventType::TestDone, 0});
  EXPECT_EQ(log.Count(), cap);

  TelemetryEvent out{};
  ASSERT_TRUE(log.GetEvent(0, out));
  EXPECT_EQ(out.ts_ms, 1u);  // ts=0 вытеснен, oldest теперь ts=1

  ASSERT_TRUE(log.GetEvent(cap - 1, out));
  EXPECT_EQ(out.ts_ms, static_cast<uint32_t>(cap));  // newest
}

// ═══════════════════════════════════════════════════════════════════════════
// Типы событий и param
// ═══════════════════════════════════════════════════════════════════════════

TEST(TelemetryEventLogTest, TestStartEvent_StoresTestType) {
  TelemetryEventLog log;
  // TestType::Circle = 2
  log.Push({500, TelemetryEventType::TestStart, 2});

  TelemetryEvent out{};
  ASSERT_TRUE(log.GetEvent(0, out));
  EXPECT_EQ(out.type, TelemetryEventType::TestStart);
  EXPECT_EQ(out.param, 2u);  // Circle
}

TEST(TelemetryEventLogTest, ImuCalibStartEvent_StoresMode) {
  TelemetryEventLog log;
  // param: 1 = full calibration
  log.Push({100, TelemetryEventType::ImuCalibStart, 1});

  TelemetryEvent out{};
  ASSERT_TRUE(log.GetEvent(0, out));
  EXPECT_EQ(out.type, TelemetryEventType::ImuCalibStart);
  EXPECT_EQ(out.param, 1u);
}

TEST(TelemetryEventLogTest, MultipleEventTypes_StoredInOrder) {
  TelemetryEventLog log;
  log.Push({100, TelemetryEventType::ImuCalibStart, 1});
  log.Push({200, TelemetryEventType::ImuCalibDone, 1});
  log.Push({300, TelemetryEventType::TrimCalibStart, 0});
  log.Push({400, TelemetryEventType::TrimCalibDone, 0});
  log.Push({500, TelemetryEventType::TestStart, 3});  // Step = 3
  log.Push({600, TelemetryEventType::TestDone, 3});

  EXPECT_EQ(log.Count(), 6u);

  TelemetryEvent out{};
  EXPECT_TRUE(log.GetEvent(4, out));
  EXPECT_EQ(out.type, TelemetryEventType::TestStart);
  EXPECT_EQ(out.param, 3u);  // Step
}
