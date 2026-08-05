#include <gtest/gtest.h>

#include "telemetry_builder.hpp"
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

TEST(TelemetryLogTest, GetFrame_TailByteFieldsRoundTrip_DoNotAlias) {
  TelemetryLog log;
  ASSERT_TRUE(log.Init(5));

  TelemetryLogFrame frame;
  frame.test_marker = 200;
  frame.zupt_status = 3;
  frame.ekf_diverged = 1;
  frame.drive_mode = static_cast<uint8_t>(4);  // DriveMode::DirectLaw
  frame.stab_enabled = 1;
  log.Push(frame);

  TelemetryLogFrame out;
  ASSERT_TRUE(log.GetFrame(0, out));
  EXPECT_EQ(out.test_marker, 200);
  EXPECT_EQ(out.zupt_status, 3);
  EXPECT_EQ(out.ekf_diverged, 1);
  EXPECT_EQ(out.drive_mode, 4);
  EXPECT_EQ(out.stab_enabled, 1);
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

TEST(TelemetryLogTest, Export_RejectsOverwrittenUnsentFrames) {
  TelemetryLog log;
  ASSERT_TRUE(log.Init(3));
  for (uint32_t ts = 1; ts <= 3; ++ts) log.Push({.ts_ms = ts});

  size_t count = 0;
  ASSERT_TRUE(log.BeginExport(count));
  ASSERT_EQ(count, 3u);
  log.Push({.ts_ms = 4});

  TelemetryLogFrame frames[3]{};
  EXPECT_EQ(log.CopyExportFrames(0, frames, 3), 0u);
  ASSERT_EQ(log.CopyExportFrames(1, frames, 2), 2u);
  EXPECT_EQ(frames[0].ts_ms, 2u);
  EXPECT_EQ(frames[1].ts_ms, 3u);
  log.EndExport();
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

// ═══════════════════════════════════════════════════════════════════════════
// kids_flags: упаковка статусов лимитеров Kids в кадр (LOS-13)
// ═══════════════════════════════════════════════════════════════════════════

namespace {

using namespace rc_vehicle;

/**
 * Кадр строится из живого KidsModeProcessor, а не из подставных булей:
 * тест должен ломаться и тогда, когда лимитер перестанет выставлять свой флаг.
 */
class BuildLogFrameKidsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    cfg_.mode = DriveMode::Kids;
    cfg_.kids_mode.limiters_enabled = true;
    cfg_.kids_mode.throttle_limit = 0.3f;
    cfg_.kids_mode.anti_spin_enabled = true;
    cfg_.kids_mode.anti_spin_threshold_deg = 10.0f;
    cfg_.kids_mode.anti_spin_reduction = 0.7f;
    cfg_.kids_mode.accel_limit_enabled = true;
    cfg_.kids_mode.accel_threshold_g = 0.15f;
    cfg_.kids_mode.accel_limit_gain = 3.0f;
    cfg_.kids_mode.accel_max_reduction = 0.5f;
    cfg_.kids_mode.speed_limit_enabled = false;

    kids_.Init(ekf_, nullptr);
  }

  /** Прогнать лимитеры с заданными заносом и продольным ускорением. */
  void RunKids(float slip_deg, float forward_accel_g) {
    const StabilizationInput input{
        .dt_ms = 10,
        .slip_angle_deg = slip_deg,
        .forward_accel_g = forward_accel_g,
        .imu_enabled = true,
    };
    float throttle = 0.3f;
    float steering = 0.0f;
    kids_.Process(cfg_, throttle, steering, input);
  }

  /**
   * Прогнать feedback speed limiter отдельным вызовом — в проде он живёт не в
   * Process(), а во втором шаге пайплайна (StabilizationPipeline::Process,
   * apply_speed_limit=false + ApplySpeedLimit), поэтому и здесь вызывается так.
   */
  void RunSpeedLimit(float speed_ms) {
    const StabilizationInput input{
        .dt_ms = 10,
        .speed_ms = speed_ms,
        .vx_variance = 0.1f,  // много ниже порога доверия скорости (LOS-285)
        .imu_enabled = true,
        .ekf_diverged = false,
    };
    float throttle = 0.3f;
    kids_.ApplySpeedLimit(cfg_, throttle, input);
  }

  uint8_t Flags(bool limiters_enabled = true) {
    const TelemetryContext ctx{ekf_,   madgwick_, imu_calib_,
                               guard_, kids_,     auto_drive_};
    return BuildLogFrame(ctx, /*now=*/100, sensors_, 0.2f, 0.0f, 0.3f, 0.0f,
                         DriveMode::Kids, /*stab_enabled=*/true,
                         limiters_enabled)
        .kids_flags;
  }

  StabilizationConfig cfg_;
  SensorSnapshot sensors_;
  VehicleEkf ekf_;
  MadgwickFilter madgwick_;
  ImuCalibration imu_calib_;
  OversteerGuard guard_;
  KidsModeProcessor kids_;
  AutoDriveCoordinator auto_drive_;
};

TEST_F(BuildLogFrameKidsTest, NoLimiterTripped_OnlyEnabledBitSet) {
  RunKids(/*slip_deg=*/0.0f, /*forward_accel_g=*/0.05f);
  EXPECT_EQ(Flags(), kKidsLimitersEnabled);
}

TEST_F(BuildLogFrameKidsTest, AntiSpinSetsOwnBit) {
  RunKids(/*slip_deg=*/25.0f, /*forward_accel_g=*/0.05f);

  const uint8_t flags = Flags();
  EXPECT_TRUE(flags & kKidsAntiSpinActive);
  EXPECT_FALSE(flags & kKidsAccelLimitActive);
  EXPECT_FALSE(flags & kKidsSpeedLimitActive);
}

TEST_F(BuildLogFrameKidsTest, AccelLimitSetsOwnBit) {
  RunKids(/*slip_deg=*/0.0f, /*forward_accel_g=*/0.25f);

  const uint8_t flags = Flags();
  EXPECT_TRUE(flags & kKidsAccelLimitActive);
  EXPECT_FALSE(flags & kKidsAntiSpinActive);
  EXPECT_FALSE(flags & kKidsSpeedLimitActive);
}

// Бит speed limiter'а (LOS-285) проверяется положительно отдельно: остальные
// тесты гоняют фикстуру со speed_limit_enabled = false, и на них перепутанная
// константа маски прошла бы незамеченной.
TEST_F(BuildLogFrameKidsTest, SpeedLimitSetsOwnBit) {
  cfg_.kids_mode.speed_limit_enabled = true;
  cfg_.kids_mode.max_speed_ms = 1.5f;
  cfg_.kids_mode.speed_limit_gain = 1.5f;

  RunSpeedLimit(/*speed_ms=*/3.0f);

  const uint8_t flags = Flags();
  EXPECT_TRUE(flags & kKidsSpeedLimitActive);
  EXPECT_FALSE(flags & kKidsAntiSpinActive);
  EXPECT_FALSE(flags & kKidsAccelLimitActive);
}

// Гистерезис: пока скорость не упала ниже release_speed, бит держится, а после
// падения — снимается. Иначе по логу нельзя отличить «лимитер держит» от
// «лимитер отпустил».
TEST_F(BuildLogFrameKidsTest, SpeedLimitBitClearsWhenSpeedDrops) {
  cfg_.kids_mode.speed_limit_enabled = true;
  cfg_.kids_mode.max_speed_ms = 1.5f;

  RunSpeedLimit(/*speed_ms=*/3.0f);
  ASSERT_NE(Flags() & kKidsSpeedLimitActive, 0);

  RunSpeedLimit(/*speed_ms=*/0.5f);
  EXPECT_EQ(Flags() & kKidsSpeedLimitActive, 0);
}

// Лимитеры независимы — маска должна показывать оба, а не «первый сработавший».
TEST_F(BuildLogFrameKidsTest, AntiSpinAndAccelLimitCombine) {
  RunKids(/*slip_deg=*/25.0f, /*forward_accel_g=*/0.25f);

  const uint8_t flags = Flags();
  EXPECT_EQ(flags & (kKidsAntiSpinActive | kKidsAccelLimitActive),
            kKidsAntiSpinActive | kKidsAccelLimitActive);
}

// LOS-286: мастер-выключатель снят — по логу это должно быть видно отдельно от
// drive_mode, иначе срез газа не отличить от «ограничители выключены».
TEST_F(BuildLogFrameKidsTest, LimitersDisabled_ClearsEnabledBit) {
  cfg_.kids_mode.limiters_enabled = false;
  RunKids(/*slip_deg=*/25.0f, /*forward_accel_g=*/0.25f);

  EXPECT_EQ(Flags(/*limiters_enabled=*/false), 0u);
}

TEST_F(BuildLogFrameKidsTest, ResetClearsMask) {
  RunKids(/*slip_deg=*/25.0f, /*forward_accel_g=*/0.25f);
  ASSERT_NE(Flags() & kKidsAntiSpinActive, 0);

  kids_.Reset();
  EXPECT_EQ(Flags(), kKidsLimitersEnabled);
}

}  // namespace
