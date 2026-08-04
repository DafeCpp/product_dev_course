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

TEST(TelemetryManagerTest, CopyLogFrames_ReturnsStableChronologicalSequence) {
  TelemetryManager mgr;
  ASSERT_TRUE(mgr.Init(3));

  for (uint32_t ts = 1; ts <= 4; ++ts) mgr.Push({.ts_ms = ts});

  TelemetryLogFrame copies[3]{};
  size_t count = 0;
  ASSERT_TRUE(mgr.BeginLogExport(count));
  ASSERT_EQ(count, 3u);
  ASSERT_EQ(mgr.CopyLogExportFrames(0, copies, 3), 3u);
  mgr.EndLogExport();
  EXPECT_EQ(copies[0].ts_ms, 2u);
  EXPECT_EQ(copies[1].ts_ms, 3u);
  EXPECT_EQ(copies[2].ts_ms, 4u);
}

TEST(TelemetryManagerTest, CombinedExportExcludesSnapshotsAfterFrameFreeze) {
  TelemetryManager mgr;
  ASSERT_TRUE(mgr.Init(3));

  rc_vehicle::StabilizationConfig cfg{};
  cfg.filter.madgwick_beta = 0.1f;
  mgr.PushConfigSnapshot(10, cfg);
  mgr.Push({.ts_ms = 10});

  size_t frame_count = 0;
  size_t snapshot_count = 0;
  TelemetryLogFrame tail{};
  ASSERT_TRUE(mgr.BeginLogAndConfigExport(frame_count, tail, snapshot_count));
  ASSERT_EQ(frame_count, 1u);
  ASSERT_EQ(tail.ts_ms, 10u);
  ASSERT_EQ(snapshot_count, 0u);

  cfg.filter.madgwick_beta = 0.2f;
  mgr.PushConfigSnapshot(10, cfg);
  ASSERT_TRUE(mgr.FinalizeConfigSnapshotExport(snapshot_count));
  ASSERT_EQ(snapshot_count, 1u);
  rc_vehicle::TelemetryConfigSnapshot exported{};
  ASSERT_TRUE(mgr.GetNextConfigSnapshotExport(exported));
  EXPECT_FALSE(mgr.GetNextConfigSnapshotExport(exported));
  EXPECT_FLOAT_EQ(exported.values[3], 0.1f);
  mgr.EndConfigSnapshotExport();
  mgr.EndLogExport();
}

// LOS-286: дельта-лог пишет снапшот только при изменившихся значениях
// (Push() выходит при value_count == 0). Если бы limiters_enabled не попал в
// схему, переключение мастер-выключателя ограничителей не оставляло бы следа в
// бинарном логе — ровно та слепота, из-за которой заводилась задача.
TEST(TelemetryManagerTest, LimitersToggleAloneIsRecordedInSnapshot) {
  TelemetryManager mgr;
  ASSERT_TRUE(mgr.Init(3));

  rc_vehicle::StabilizationConfig cfg{};
  cfg.mode = rc_vehicle::DriveMode::Kids;
  ASSERT_TRUE(cfg.kids_mode.limiters_enabled);
  mgr.PushConfigSnapshot(10, cfg);

  // Единственное изменение во всём конфиге.
  cfg.kids_mode.limiters_enabled = false;
  mgr.PushConfigSnapshot(20, cfg);

  size_t snapshot_count = 0;
  ASSERT_TRUE(mgr.BeginConfigSnapshotExport(/*max_ts_ms=*/20, snapshot_count));
  ASSERT_EQ(snapshot_count, 2u);

  rc_vehicle::TelemetryConfigSnapshot first{};
  rc_vehicle::TelemetryConfigSnapshot second{};
  ASSERT_TRUE(mgr.GetNextConfigSnapshotExport(first));
  ASSERT_TRUE(mgr.GetNextConfigSnapshotExport(second));
  mgr.EndConfigSnapshotExport();

  EXPECT_EQ(first.schema_version,
            rc_vehicle::TelemetryConfigSnapshot::kSchemaVersion);
  EXPECT_NE(first.values[63], second.values[63]);
  EXPECT_FLOAT_EQ(first.values[63], 1.0f);
  EXPECT_FLOAT_EQ(second.values[63], 0.0f);
}

TEST(TelemetryManagerTest, CombinedExportOrdersSameTimestampTransitions) {
  TelemetryManager mgr;
  ASSERT_TRUE(mgr.Init(3));

  rc_vehicle::StabilizationConfig cfg{};
  cfg.filter.madgwick_beta = 0.1f;
  mgr.PushConfigSnapshot(10, cfg);
  mgr.Push({.ts_ms = 10});
  cfg.filter.madgwick_beta = 0.2f;
  mgr.PushConfigSnapshot(10, cfg);
  mgr.Push({.ts_ms = 10});

  size_t frame_count = 0;
  size_t snapshot_count = 0;
  TelemetryLogFrame tail{};
  ASSERT_TRUE(mgr.BeginLogAndConfigExport(frame_count, tail, snapshot_count));
  ASSERT_TRUE(mgr.FinalizeConfigSnapshotExport(snapshot_count));
  ASSERT_EQ(snapshot_count, 2u);

  rc_vehicle::TelemetryConfigSnapshot baseline{};
  rc_vehicle::TelemetryConfigSnapshot transition{};
  ASSERT_TRUE(mgr.GetNextConfigSnapshotExport(baseline));
  ASSERT_TRUE(mgr.GetNextConfigSnapshotExport(transition));
  EXPECT_EQ(baseline.frame_index, 0u);
  EXPECT_EQ(transition.frame_index, 1u);
  mgr.EndConfigSnapshotExport();
  mgr.EndLogExport();
}

TEST(TelemetryManagerTest, ConfigSnapshot_PreservesAppliedConfig) {
  TelemetryManager mgr;
  rc_vehicle::StabilizationConfig cfg{};
  cfg.enabled = true;
  cfg.filter.madgwick_beta = 0.25f;
  cfg.kids_mode.max_speed_ms = 2.5f;

  mgr.PushConfigSnapshot(1234, cfg);

  rc_vehicle::TelemetryConfigSnapshot out{};
  ASSERT_TRUE(mgr.GetConfigSnapshot(0, out));
  EXPECT_EQ(out.ts_ms, 1234u);
  EXPECT_EQ(out.schema_version,
            rc_vehicle::TelemetryConfigSnapshot::kSchemaVersion);
  EXPECT_FLOAT_EQ(out.values[0], 1.0f);
  EXPECT_FLOAT_EQ(out.values[3], 0.25f);
  EXPECT_FLOAT_EQ(out.values[55], 2.5f);

  mgr.ClearConfigSnapshots();
  EXPECT_EQ(mgr.GetConfigSnapshotCount(), 0u);
}

TEST(TelemetryManagerTest, ConfigSnapshot_RetainsBaselineWhenFramesWrap) {
  TelemetryManager mgr;
  ASSERT_TRUE(mgr.Init(2));

  rc_vehicle::StabilizationConfig cfg{};
  cfg.filter.madgwick_beta = 0.2f;
  mgr.PushConfigSnapshot(10, cfg);
  mgr.Push({.ts_ms = 10});
  mgr.Push({.ts_ms = 20});
  mgr.Push({.ts_ms = 30});

  rc_vehicle::TelemetryConfigSnapshot baseline{};
  ASSERT_TRUE(mgr.GetConfigSnapshot(0, baseline));
  EXPECT_EQ(baseline.ts_ms, 10u);
  EXPECT_FLOAT_EQ(baseline.values[3], 0.2f);
}

TEST(TelemetryManagerTest, ConfigSnapshot_DeltaLogRetainsManyTransitions) {
  rc_vehicle::TelemetryConfigSnapshotLog snapshots;
  constexpr uint32_t kTransitions = 52000;
  for (uint32_t ts = 0; ts < kTransitions; ++ts) {
    rc_vehicle::TelemetryConfigSnapshot snapshot{.ts_ms = ts};
    snapshot.values[3] = static_cast<float>(ts);
    snapshots.Push(snapshot);
  }

  ASSERT_EQ(snapshots.Count(), kTransitions);
  rc_vehicle::TelemetryConfigSnapshot first{};
  rc_vehicle::TelemetryConfigSnapshot last{};
  ASSERT_TRUE(snapshots.GetSnapshot(0, first));
  ASSERT_TRUE(snapshots.GetSnapshot(snapshots.Count() - 1, last));
  EXPECT_EQ(first.ts_ms, 0u);
  EXPECT_EQ(last.ts_ms, kTransitions - 1);
  EXPECT_FLOAT_EQ(last.values[3], static_cast<float>(kTransitions - 1));
}

TEST(TelemetryManagerTest, ConfigSnapshot_ExportStopsAtFrozenFrameTail) {
  rc_vehicle::TelemetryConfigSnapshotLog snapshots;
  for (uint32_t ts : {10u, 20u, 30u}) {
    rc_vehicle::TelemetryConfigSnapshot snapshot{.ts_ms = ts};
    snapshot.values[3] = static_cast<float>(ts);
    snapshots.Push(snapshot);
  }

  size_t count = 0;
  ASSERT_TRUE(snapshots.BeginExport(20, count));
  ASSERT_EQ(count, 2u);
  rc_vehicle::TelemetryConfigSnapshot first{};
  rc_vehicle::TelemetryConfigSnapshot second{};
  ASSERT_TRUE(snapshots.GetNextExportSnapshot(first));
  ASSERT_TRUE(snapshots.GetNextExportSnapshot(second));
  EXPECT_FALSE(snapshots.GetNextExportSnapshot(second));
  snapshots.EndExport();
  EXPECT_EQ(first.ts_ms, 10u);
  EXPECT_EQ(second.ts_ms, 20u);
}

TEST(TelemetryManagerTest, ConfigSnapshot_ExportPinsBaselineDuringFrameWrap) {
  TelemetryManager mgr;
  ASSERT_TRUE(mgr.Init(2));

  rc_vehicle::StabilizationConfig cfg{};
  cfg.filter.madgwick_beta = 0.1f;
  mgr.PushConfigSnapshot(10, cfg);
  mgr.Push({.ts_ms = 10});
  cfg.filter.madgwick_beta = 0.2f;
  mgr.PushConfigSnapshot(20, cfg);
  mgr.Push({.ts_ms = 20});

  size_t count = 0;
  ASSERT_TRUE(mgr.BeginConfigSnapshotExport(20, count));
  mgr.Push({.ts_ms = 30});
  mgr.Push({.ts_ms = 40});

  rc_vehicle::TelemetryConfigSnapshot baseline{};
  ASSERT_TRUE(mgr.GetNextConfigSnapshotExport(baseline));
  mgr.EndConfigSnapshotExport();
  EXPECT_EQ(baseline.ts_ms, 10u);
  EXPECT_FLOAT_EQ(baseline.values[3], 0.1f);
}

TEST(TelemetryManagerTest, ConfigSnapshot_OverflowResetsAssociatedHistory) {
  TelemetryManager mgr;
  ASSERT_TRUE(mgr.Init(5));
  for (uint32_t ts = 1; ts <= 5; ++ts) mgr.Push({.ts_ms = ts});

  auto* snapshots = mgr.GetConfigSnapshotLog();
  for (uint32_t ts = 1; ts <= 4000; ++ts) {
    rc_vehicle::TelemetryConfigSnapshot snapshot{.ts_ms = ts};
    for (float& value : snapshot.values) value = static_cast<float>(ts);
    snapshots->Push(snapshot);
  }

  mgr.Push({.ts_ms = 5000});
  size_t frame_count = 0;
  size_t capacity = 0;
  mgr.GetLogInfo(frame_count, capacity);
  EXPECT_EQ(frame_count, 1u);
  rc_vehicle::TelemetryConfigSnapshot baseline{};
  ASSERT_TRUE(mgr.GetConfigSnapshot(0, baseline));
  EXPECT_EQ(baseline.ts_ms, 4000u);
}

TEST(TelemetryManagerTest, ConfigSnapshot_OverflowResetKeepsNextTransition) {
  rc_vehicle::TelemetryConfigSnapshotLog snapshots;
  for (uint32_t ts = 1; ts <= 4000; ++ts) {
    rc_vehicle::TelemetryConfigSnapshot snapshot{.ts_ms = ts};
    for (float& value : snapshot.values) value = static_cast<float>(ts);
    snapshots.Push(snapshot);
  }

  ASSERT_TRUE(snapshots.ResetAfterOverflow());
  rc_vehicle::TelemetryConfigSnapshot next{.ts_ms = 5000};
  next.values[3] = 42.0f;
  snapshots.Push(next);

  ASSERT_EQ(snapshots.Count(), 2u);
  rc_vehicle::TelemetryConfigSnapshot restored{};
  ASSERT_TRUE(snapshots.GetSnapshot(1, restored));
  EXPECT_EQ(restored.ts_ms, 5000u);
  EXPECT_FLOAT_EQ(restored.values[3], 42.0f);
}

TEST(TelemetryManagerTest, ConfigSnapshot_OverflowResetWaitsForExport) {
  TelemetryManager mgr;
  ASSERT_TRUE(mgr.Init(5));
  for (uint32_t ts = 1; ts <= 5; ++ts) mgr.Push({.ts_ms = ts});

  auto* snapshots = mgr.GetConfigSnapshotLog();
  for (uint32_t ts = 1; ts <= 4000; ++ts) {
    rc_vehicle::TelemetryConfigSnapshot snapshot{.ts_ms = ts};
    for (float& value : snapshot.values) value = static_cast<float>(ts);
    snapshots->Push(snapshot);
  }

  size_t snapshot_count = 0;
  ASSERT_TRUE(mgr.BeginConfigSnapshotExport(5, snapshot_count));
  mgr.Push({.ts_ms = 5000});

  size_t frame_count = 0;
  size_t capacity = 0;
  mgr.GetLogInfo(frame_count, capacity);
  EXPECT_EQ(frame_count, capacity);
  rc_vehicle::TelemetryConfigSnapshot exported{};
  EXPECT_TRUE(mgr.GetNextConfigSnapshotExport(exported));
  mgr.EndConfigSnapshotExport();

  mgr.Push({.ts_ms = 6000});
  mgr.GetLogInfo(frame_count, capacity);
  EXPECT_EQ(frame_count, 1u);
}

TEST(TelemetryManagerTest, ConfigSnapshot_PrunesAcrossTimestampWrap) {
  rc_vehicle::TelemetryConfigSnapshotLog snapshots;
  rc_vehicle::TelemetryConfigSnapshot before_wrap{.ts_ms = 0xfffffff0};
  rc_vehicle::TelemetryConfigSnapshot after_wrap{.ts_ms = 0x00000010};
  after_wrap.values[3] = 0.5f;
  snapshots.Push(before_wrap);
  snapshots.Push(after_wrap);

  snapshots.PruneBefore(0xfffffff0);

  ASSERT_EQ(snapshots.Count(), 2u);
  rc_vehicle::TelemetryConfigSnapshot baseline{};
  rc_vehicle::TelemetryConfigSnapshot post_wrap{};
  ASSERT_TRUE(snapshots.GetSnapshot(0, baseline));
  ASSERT_TRUE(snapshots.GetSnapshot(1, post_wrap));
  EXPECT_EQ(baseline.ts_ms, 0xfffffff0u);
  EXPECT_EQ(post_wrap.ts_ms, 0x00000010u);
}

TEST(TelemetryManagerTest, ConfigSnapshot_PrunesAfterLongTimestampGap) {
  TelemetryManager mgr;
  ASSERT_TRUE(mgr.Init(2));

  rc_vehicle::StabilizationConfig cfg{};
  cfg.filter.madgwick_beta = 0.1f;
  mgr.PushConfigSnapshot(1, cfg);
  mgr.Push({.ts_ms = 1});
  cfg.filter.madgwick_beta = 0.2f;
  mgr.PushConfigSnapshot(2, cfg);
  mgr.Push({.ts_ms = 2});
  mgr.Push({.ts_ms = 0x80000020u});
  mgr.Push({.ts_ms = 0x80000030u});

  rc_vehicle::TelemetryConfigSnapshot baseline{};
  ASSERT_TRUE(mgr.GetConfigSnapshot(0, baseline));
  EXPECT_EQ(baseline.ts_ms, 2u);
  EXPECT_FLOAT_EQ(baseline.values[3], 0.2f);
}
