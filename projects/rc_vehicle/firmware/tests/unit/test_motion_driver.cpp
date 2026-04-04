#include <gtest/gtest.h>

#include "motion_driver.hpp"

using namespace rc_vehicle;

// ═══════════════════════════════════════════════════════════════════════════
// Helpers
// ═══════════════════════════════════════════════════════════════════════════

static MotionDriver::Config DefaultPidConfig(float target_accel_g = 0.1f) {
  return {
      .accel_mode = MotionDriver::AccelMode::Pid,
      .pid_gains = {0.3f, 0.2f, 0.0f, 0.15f, 0.5f},
      .target_value = target_accel_g,
      .accel_duration_sec = 1.5f,
      .min_effective_throttle = 0.0f,
      .brake_throttle = 0.0f,
      .brake_timeout_sec = 3.0f,
      .zupt = {0.05f, 3.0f},
      .breakaway = {0.5f, 0.25f, 0.03f, 25},
  };
}

static MotionDriver::Config LinearRampConfig(float target_throttle = 0.3f) {
  return {
      .accel_mode = MotionDriver::AccelMode::LinearRamp,
      .target_value = target_throttle,
      .accel_duration_sec = 1.5f,
      .min_effective_throttle = 0.15f,
      .brake_throttle = -0.4f,
      .brake_timeout_sec = 3.0f,
      .zupt = {0.05f, 0.0f},  // no gyro check
  };
}

/** Прокрутить N тиков с заданными параметрами. */
static void RunTicks(MotionDriver& d, int n, float dt,
                     float accel_g = 0.1f, float accel_mag = 1.0f,
                     float gz = 0.0f) {
  for (int i = 0; i < n; ++i) {
    d.Update(accel_g, accel_mag, gz, dt);
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// Basic lifecycle
// ═══════════════════════════════════════════════════════════════════════════

TEST(MotionDriverTest, InitialPhase_IsIdle) {
  MotionDriver d;
  EXPECT_EQ(d.GetPhase(), MotionPhase::Idle);
}

TEST(MotionDriverTest, Start_TransitionsToAccelerate) {
  MotionDriver d;
  d.Start(DefaultPidConfig());
  EXPECT_EQ(d.GetPhase(), MotionPhase::Accelerate);
}

TEST(MotionDriverTest, Reset_TransitionsToIdle) {
  MotionDriver d;
  d.Start(DefaultPidConfig());
  d.Reset();
  EXPECT_EQ(d.GetPhase(), MotionPhase::Idle);
}

TEST(MotionDriverTest, Update_IdleReturnsZero) {
  MotionDriver d;
  EXPECT_FLOAT_EQ(d.Update(0, 1.0f, 0, 0.002f), 0.0f);
}

TEST(MotionDriverTest, Update_ZeroDtReturnsZero) {
  MotionDriver d;
  d.Start(DefaultPidConfig());
  EXPECT_FLOAT_EQ(d.Update(0, 1.0f, 0, 0.0f), 0.0f);
  EXPECT_EQ(d.GetPhase(), MotionPhase::Accelerate);  // no transition
}

// ═══════════════════════════════════════════════════════════════════════════
// PID Acceleration — Breakaway ramp (Phase A)
// ═══════════════════════════════════════════════════════════════════════════

TEST(MotionDriverTest, PidAccel_BreakawayRamp_ProducesPositiveThrottle) {
  MotionDriver d;
  d.Start(DefaultPidConfig(0.1f));
  // accel=0 → breakaway ramp: throttle = 0.5 * 0.002 = 0.001
  float thr = d.Update(0.0f, 1.0f, 0.0f, 0.002f);
  EXPECT_GT(thr, 0.0f);
}

TEST(MotionDriverTest, PidAccel_BreakawayRamp_IncreasesLinearly) {
  MotionDriver d;
  d.Start(DefaultPidConfig());
  // ramp_rate=0.5, dt=0.002 → after 100 ticks (0.2s): throttle ≈ 0.5*0.2=0.1
  float thr1 = d.Update(0.0f, 1.0f, 0.0f, 0.002f);
  RunTicks(d, 99, 0.002f, 0.0f);
  float thr100 = d.Update(0.0f, 1.0f, 0.0f, 0.002f);
  EXPECT_GT(thr100, thr1);
  EXPECT_NEAR(thr100, 0.1f, 0.01f);
}

TEST(MotionDriverTest, PidAccel_BreakawayRamp_ClampsToMax) {
  MotionDriver d;
  d.Start(DefaultPidConfig());
  // ramp_rate=0.5, max=0.25 → reached at t=0.5s (250 ticks)
  // Run 300 ticks (0.6s) with accel=0 → still in breakaway
  RunTicks(d, 300, 0.002f, 0.0f);
  // At 250 ticks (0.5s), fallback triggered → now in PI mode
  // base_throttle = 0.25, PI correction with error=0.1, kp=0.3 → +0.03
  // But we're past fallback, so throttle ≈ base + correction
  float thr = d.Update(0.0f, 1.0f, 0.0f, 0.002f);
  EXPECT_GT(thr, 0.0f);
}

TEST(MotionDriverTest, PidAccel_BreakawayDetected_WhenAccelAboveThreshold) {
  MotionDriver d;
  auto cfg = DefaultPidConfig(0.1f);
  cfg.breakaway.confirm_ticks = 5;  // fast confirm for test
  d.Start(cfg);

  // Run 50 ticks (0.1s) with accel=0 → ramp phase
  RunTicks(d, 50, 0.002f, 0.0f);
  float ramp_thr = d.Update(0.0f, 1.0f, 0.0f, 0.002f);

  // Now feed accel > thresh (0.03g) for 5 ticks → breakaway detected
  for (int i = 0; i < 5; ++i) {
    d.Update(0.05f, 1.0f, 0.0f, 0.002f);
  }

  // After breakaway: PI mode. Feed accel=target → error≈0 → throttle ≈ base
  float pi_thr = d.Update(0.1f, 1.0f, 0.0f, 0.002f);
  // base_throttle was ramp value at t≈0.114s → ~0.057
  // PI correction: error=0 → correction≈0
  EXPECT_GT(pi_thr, 0.0f);
  EXPECT_NEAR(pi_thr, ramp_thr, 0.05f);  // close to ramp value at breakaway
}

TEST(MotionDriverTest, PidAccel_BreakawayConfirmResets_OnDrop) {
  MotionDriver d;
  auto cfg = DefaultPidConfig();
  cfg.breakaway.confirm_ticks = 10;
  d.Start(cfg);

  // 5 ticks above threshold, then drop
  for (int i = 0; i < 5; ++i) {
    d.Update(0.05f, 1.0f, 0.0f, 0.002f);
  }
  d.Update(0.0f, 1.0f, 0.0f, 0.002f);  // drop → confirm resets

  // 5 more ticks above threshold → still not enough (need 10 consecutive)
  for (int i = 0; i < 5; ++i) {
    d.Update(0.05f, 1.0f, 0.0f, 0.002f);
  }
  // Still in Accelerate (breakaway not yet confirmed, ramp still going)
  EXPECT_EQ(d.GetPhase(), MotionPhase::Accelerate);
}

TEST(MotionDriverTest, PidAccel_BreakawayFallback_WhenRampReachesMax) {
  MotionDriver d;
  d.Start(DefaultPidConfig());
  // ramp_rate=0.5, max=0.25 → fallback at t=0.5s (250 ticks)
  // Feed accel=0 entire time → no accel-based breakaway
  RunTicks(d, 249, 0.002f, 0.0f);
  // At tick 250: ramp = 0.5*0.5 = 0.25 >= max → fallback
  float thr = d.Update(0.0f, 1.0f, 0.0f, 0.002f);
  // Now in PI mode: base=0.25, error=0.1, kp=0.3 → correction=0.03
  // Next tick should show PI behavior
  float thr2 = d.Update(0.0f, 1.0f, 0.0f, 0.002f);
  EXPECT_GT(thr2, 0.25f);  // base + positive PI correction
}

TEST(MotionDriverTest, PidAccel_AfterBreakaway_PiCorrects) {
  MotionDriver d;
  auto cfg = DefaultPidConfig(0.1f);
  cfg.breakaway.confirm_ticks = 1;  // instant breakaway
  d.Start(cfg);

  // Trigger immediate breakaway
  d.Update(0.05f, 1.0f, 0.0f, 0.002f);

  // Overshoot: measured > target → error < 0 → PI should reduce
  float thr_high = d.Update(0.3f, 1.0f, 0.0f, 0.002f);
  float thr_higher = d.Update(0.3f, 1.0f, 0.0f, 0.002f);
  // With negative error, correction decreases
  EXPECT_LE(thr_higher, thr_high);
}

TEST(MotionDriverTest, PidAccel_AfterBreakaway_IntegralStartsClean) {
  MotionDriver d;
  auto cfg = DefaultPidConfig(0.1f);
  cfg.breakaway.confirm_ticks = 1;
  d.Start(cfg);

  // Run 100 ticks of ramp (PID not active, integral should be 0)
  RunTicks(d, 100, 0.002f, 0.0f);

  // Trigger breakaway
  d.Update(0.05f, 1.0f, 0.0f, 0.002f);

  // First PI tick: only P-term (integral ≈ 0 since just reset)
  float thr = d.Update(0.0f, 1.0f, 0.0f, 0.002f);
  // base ≈ 0.5*0.202=0.101, P-term = 0.3*0.1 = 0.03
  // If integral had accumulated during ramp, throttle would be much higher
  EXPECT_LT(thr, 0.3f);  // well below what windup would produce
}

TEST(MotionDriverTest, PidAccel_TransitionsToCruiseAfterDuration) {
  MotionDriver d;
  d.Start(DefaultPidConfig());
  // 1.5s / 0.002s = 750 ticks; use 760 for float margin
  RunTicks(d, 760, 0.002f, 0.0f);
  EXPECT_EQ(d.GetPhase(), MotionPhase::Cruise);
  EXPECT_GT(d.GetCruiseThrottle(), 0.0f);
}

TEST(MotionDriverTest, PidAccel_PhaseElapsedResets) {
  MotionDriver d;
  d.Start(DefaultPidConfig());
  RunTicks(d, 760, 0.002f, 0.0f);
  EXPECT_EQ(d.GetPhase(), MotionPhase::Cruise);
  EXPECT_LT(d.GetPhaseElapsed(), 0.05f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Linear Ramp Acceleration
// ═══════════════════════════════════════════════════════════════════════════

TEST(MotionDriverTest, LinearRamp_IncreasesOverTime) {
  MotionDriver d;
  d.Start(LinearRampConfig(0.3f));
  float thr1 = d.Update(0, 1.0f, 0, 0.002f);
  // skip to t=0.75s (half of accel duration)
  RunTicks(d, 375, 0.002f);
  float thr2 = d.Update(0, 1.0f, 0, 0.002f);
  EXPECT_GT(thr2, thr1);
}

TEST(MotionDriverTest, LinearRamp_ReachesTargetAtEnd) {
  MotionDriver d;
  d.Start(LinearRampConfig(0.3f));
  // Run to just before transition
  RunTicks(d, 760, 0.002f);
  d.Update(0, 1.0f, 0, 0.002f);
  EXPECT_EQ(d.GetPhase(), MotionPhase::Cruise);
  EXPECT_NEAR(d.GetCruiseThrottle(), 0.3f, 0.02f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Cruise
// ═══════════════════════════════════════════════════════════════════════════

TEST(MotionDriverTest, Cruise_ReturnsFrozenThrottle) {
  MotionDriver d;
  d.Start(DefaultPidConfig());
  RunTicks(d, 760, 0.002f, 0.0f);
  ASSERT_EQ(d.GetPhase(), MotionPhase::Cruise);
  float frozen = d.GetCruiseThrottle();
  float thr = d.Update(0, 1.0f, 0, 0.002f);
  EXPECT_FLOAT_EQ(thr, frozen);
}

TEST(MotionDriverTest, Cruise_EndCruise_TransitionsToBrake) {
  MotionDriver d;
  d.Start(DefaultPidConfig());
  RunTicks(d, 760, 0.002f, 0.0f);
  ASSERT_EQ(d.GetPhase(), MotionPhase::Cruise);
  d.EndCruise();
  EXPECT_EQ(d.GetPhase(), MotionPhase::Brake);
  EXPECT_NEAR(d.GetPhaseElapsed(), 0.0f, 0.001f);
}

TEST(MotionDriverTest, EndCruise_NotInCruise_NoOp) {
  MotionDriver d;
  d.Start(DefaultPidConfig());
  EXPECT_EQ(d.GetPhase(), MotionPhase::Accelerate);
  d.EndCruise();  // should be no-op
  EXPECT_EQ(d.GetPhase(), MotionPhase::Accelerate);
}

// ═══════════════════════════════════════════════════════════════════════════
// Brake — ZUPT with gyro
// ═══════════════════════════════════════════════════════════════════════════

TEST(MotionDriverTest, Brake_CoastThrottle) {
  MotionDriver d;
  d.Start(DefaultPidConfig());
  RunTicks(d, 760, 0.002f, 0.0f);
  d.EndCruise();
  // Not stopped yet: large accel magnitude
  float thr = d.Update(0, 2.0f, 50.0f, 0.002f);
  EXPECT_FLOAT_EQ(thr, 0.0f);  // coast
  EXPECT_EQ(d.GetPhase(), MotionPhase::Brake);
}

TEST(MotionDriverTest, Brake_ZuptStopsWhenBothBelow) {
  MotionDriver d;
  d.Start(DefaultPidConfig());
  RunTicks(d, 760, 0.002f, 0.0f);
  d.EndCruise();
  // accel_mag ≈ 1.0g, gyro ≈ 0 → ZUPT triggers
  d.Update(0, 1.0f, 0.0f, 0.002f);
  EXPECT_EQ(d.GetPhase(), MotionPhase::Stopped);
}

TEST(MotionDriverTest, Brake_NotStoppedIfGyroHigh) {
  MotionDriver d;
  d.Start(DefaultPidConfig());
  RunTicks(d, 760, 0.002f, 0.0f);
  d.EndCruise();
  // accel_mag OK but gyro too high
  d.Update(0, 1.0f, 10.0f, 0.002f);
  EXPECT_EQ(d.GetPhase(), MotionPhase::Brake);
}

TEST(MotionDriverTest, Brake_TimeoutStops) {
  MotionDriver d;
  d.Start(DefaultPidConfig());
  RunTicks(d, 760, 0.002f, 0.0f);
  d.EndCruise();
  // Not stopped, but run past timeout (3.0s)
  RunTicks(d, 1510, 0.002f, 0.0f, 2.0f, 50.0f);
  EXPECT_EQ(d.GetPhase(), MotionPhase::Stopped);
}

// ═══════════════════════════════════════════════════════════════════════════
// Brake — Reverse + no gyro (SpeedCalibration style)
// ═══════════════════════════════════════════════════════════════════════════

TEST(MotionDriverTest, ReverseBrake_NegativeThrottle) {
  MotionDriver d;
  d.Start(LinearRampConfig());
  RunTicks(d, 760, 0.002f);
  d.EndCruise();
  // Not stopped: accel_mag far from 1g
  float thr = d.Update(0, 2.0f, 50.0f, 0.002f);
  EXPECT_FLOAT_EQ(thr, -0.4f);
}

TEST(MotionDriverTest, NoGyroZupt_StopsWithAccelOnly) {
  MotionDriver d;
  d.Start(LinearRampConfig());
  RunTicks(d, 760, 0.002f);
  d.EndCruise();
  // accel_mag ≈ 1g, gyro high but ignored (gyro_thresh=0)
  d.Update(0, 1.0f, 100.0f, 0.002f);
  EXPECT_EQ(d.GetPhase(), MotionPhase::Stopped);
}

// ═══════════════════════════════════════════════════════════════════════════
// Repeated Start() resets state
// ═══════════════════════════════════════════════════════════════════════════

TEST(MotionDriverTest, RepeatedStart_ResetsCompletely) {
  MotionDriver d;
  d.Start(DefaultPidConfig());
  RunTicks(d, 760, 0.002f, 0.0f);
  d.EndCruise();
  d.Update(0, 1.0f, 0.0f, 0.002f);
  ASSERT_EQ(d.GetPhase(), MotionPhase::Stopped);

  // Start again
  d.Start(DefaultPidConfig(0.2f));
  EXPECT_EQ(d.GetPhase(), MotionPhase::Accelerate);
  EXPECT_NEAR(d.GetPhaseElapsed(), 0.0f, 0.001f);
  EXPECT_FLOAT_EQ(d.GetCruiseThrottle(), 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Full cycle: Accelerate → Cruise → Brake → Stopped
// ═══════════════════════════════════════════════════════════════════════════

TEST(MotionDriverTest, FullCycle_PidCoastBrake) {
  MotionDriver d;
  d.Start(DefaultPidConfig());

  // Accelerate (1.5s)
  RunTicks(d, 760, 0.002f, 0.0f);
  ASSERT_EQ(d.GetPhase(), MotionPhase::Cruise);

  // Cruise (some time)
  RunTicks(d, 500, 0.002f, 0.0f);
  ASSERT_EQ(d.GetPhase(), MotionPhase::Cruise);

  // End cruise
  d.EndCruise();
  ASSERT_EQ(d.GetPhase(), MotionPhase::Brake);

  // ZUPT: stopped
  d.Update(0, 1.0f, 0.0f, 0.002f);
  EXPECT_EQ(d.GetPhase(), MotionPhase::Stopped);
}

TEST(MotionDriverTest, FullCycle_LinearRampReverseBrake) {
  MotionDriver d;
  d.Start(LinearRampConfig(0.3f));

  // Accelerate
  RunTicks(d, 760, 0.002f);
  ASSERT_EQ(d.GetPhase(), MotionPhase::Cruise);

  // End cruise
  d.EndCruise();
  ASSERT_EQ(d.GetPhase(), MotionPhase::Brake);

  // Still moving (high accel_mag)
  float thr = d.Update(0, 2.0f, 0, 0.002f);
  EXPECT_FLOAT_EQ(thr, -0.4f);
  EXPECT_EQ(d.GetPhase(), MotionPhase::Brake);

  // Stopped (accel_mag ≈ 1g, gyro ignored)
  d.Update(0, 1.0f, 100.0f, 0.002f);
  EXPECT_EQ(d.GetPhase(), MotionPhase::Stopped);
}

TEST(MotionDriverTest, Stopped_UpdateReturnsZero) {
  MotionDriver d;
  d.Start(DefaultPidConfig());
  RunTicks(d, 760, 0.002f, 0.0f);
  d.EndCruise();
  d.Update(0, 1.0f, 0.0f, 0.002f);
  ASSERT_EQ(d.GetPhase(), MotionPhase::Stopped);
  EXPECT_FLOAT_EQ(d.Update(0, 1.0f, 0.0f, 0.002f), 0.0f);
}
