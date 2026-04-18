#include <gtest/gtest.h>

#include "speed_calibration.hpp"

namespace rc_vehicle {
namespace {

class SpeedCalibrationTest : public ::testing::Test {
 protected:
  SpeedCalibration calib;
  static constexpr float kDt = 0.002f;  // 500 Hz

  void Step(float speed_ms, float accel_mag, float& throttle, float& steering) {
    calib.Update(speed_ms, accel_mag, kDt, throttle, steering);
  }

  // Run until accelerate phase ends (extra margin for fp accumulation)
  void RunAccelPhase(float speed_ms, float& throttle, float& steering) {
    const int max_steps = static_cast<int>(1.5f / kDt) + 20;
    for (int i = 0; i < max_steps; ++i) {
      Step(speed_ms, 1.0f, throttle, steering);
      if (calib.GetPhase() != SpeedCalibration::Phase::Accelerate) break;
    }
  }

  // Run until cruise phase ends (extra margin for fp accumulation)
  void RunCruisePhase(float speed_ms, float cruise_sec, float& throttle,
                      float& steering) {
    const int max_steps = static_cast<int>(cruise_sec / kDt) + 20;
    for (int i = 0; i < max_steps; ++i) {
      Step(speed_ms, 1.0f, throttle, steering);
      if (calib.GetPhase() != SpeedCalibration::Phase::Cruise) break;
    }
  }

  // Simulate stop (accel_mag < 0.05g)
  void RunBrakePhaseUntilStop(float& throttle, float& steering) {
    Step(0.0f, 0.01f, throttle, steering);  // ZUPT
  }

  // Full calibration run
  SpeedCalibration::Result RunFull(float speed_ms, float target_thr = 0.3f,
                                   float cruise_sec = 3.0f) {
    calib.Start(target_thr, cruise_sec);
    float thr = 0, str = 0;
    RunAccelPhase(speed_ms, thr, str);
    RunCruisePhase(speed_ms, cruise_sec, thr, str);
    RunBrakePhaseUntilStop(thr, str);
    return calib.GetResult();
  }
};

// ── Basic lifecycle ──────────────────────────────────────────────────────────

TEST_F(SpeedCalibrationTest, StartFromIdle) {
  EXPECT_TRUE(calib.Start());
  EXPECT_TRUE(calib.IsActive());
  EXPECT_EQ(calib.GetPhase(), SpeedCalibration::Phase::Accelerate);
}

TEST_F(SpeedCalibrationTest, CannotStartWhileActive) {
  EXPECT_TRUE(calib.Start());
  EXPECT_FALSE(calib.Start());
}

TEST_F(SpeedCalibrationTest, InitiallyIdle) {
  EXPECT_FALSE(calib.IsActive());
  EXPECT_FALSE(calib.IsFinished());
  EXPECT_EQ(calib.GetPhase(), SpeedCalibration::Phase::Idle);
}

TEST_F(SpeedCalibrationTest, StopClearsState) {
  calib.Start();
  EXPECT_TRUE(calib.IsActive());
  calib.Stop();
  EXPECT_FALSE(calib.IsActive());
  EXPECT_EQ(calib.GetPhase(), SpeedCalibration::Phase::Idle);
}

TEST_F(SpeedCalibrationTest, CanRestartAfterDone) {
  auto result = RunFull(1.5f);
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(calib.Start());
}

TEST_F(SpeedCalibrationTest, CanRestartAfterStop) {
  calib.Start();
  calib.Stop();
  EXPECT_TRUE(calib.Start());
}

// ── Parameter validation ─────────────────────────────────────────────────────

TEST_F(SpeedCalibrationTest, RejectsThrottleTooLow) {
  EXPECT_FALSE(calib.Start(0.05f, 3.0f));
  EXPECT_FALSE(calib.IsActive());
}

TEST_F(SpeedCalibrationTest, RejectsThrottleTooHigh) {
  EXPECT_FALSE(calib.Start(0.9f, 3.0f));
  EXPECT_FALSE(calib.IsActive());
}

TEST_F(SpeedCalibrationTest, RejectsDurationTooShort) {
  EXPECT_FALSE(calib.Start(0.3f, 0.5f));
  EXPECT_FALSE(calib.IsActive());
}

TEST_F(SpeedCalibrationTest, RejectsDurationTooLong) {
  EXPECT_FALSE(calib.Start(0.3f, 15.0f));
  EXPECT_FALSE(calib.IsActive());
}

TEST_F(SpeedCalibrationTest, AcceptsValidParameters) {
  EXPECT_TRUE(calib.Start(0.1f, 1.0f));
}

TEST_F(SpeedCalibrationTest, AcceptsMaxParameters) {
  EXPECT_TRUE(calib.Start(0.8f, 10.0f));
}

// ── Accelerate phase ─────────────────────────────────────────────────────────

TEST_F(SpeedCalibrationTest, AcceleratePhaseRampsThrottle) {
  calib.Start(0.4f, 3.0f);
  float thr = 0, str = 0;
  // At start, ramp = 0
  calib.Update(0.0f, 1.0f, 0.0f, thr, str);
  EXPECT_NEAR(thr, 0.0f, 0.01f);
  EXPECT_EQ(str, 0.0f);

  // After 0.75s (half of kAccelSec=1.5s), ramp ≈ 0.5
  for (int i = 0; i < 375; ++i) {
    calib.Update(0.0f, 1.0f, kDt, thr, str);
  }
  EXPECT_NEAR(thr, 0.2f, 0.02f);  // 0.5 * 0.4
}

TEST_F(SpeedCalibrationTest, AcceleratePhaseTransitionsToCruise) {
  calib.Start(0.3f, 3.0f);
  float thr = 0, str = 0;
  RunAccelPhase(1.0f, thr, str);
  EXPECT_EQ(calib.GetPhase(), SpeedCalibration::Phase::Cruise);
}

TEST_F(SpeedCalibrationTest, CruisePhaseHoldsThrottle) {
  calib.Start(0.35f, 3.0f);
  float thr = 0, str = 0;
  RunAccelPhase(1.0f, thr, str);
  EXPECT_EQ(calib.GetPhase(), SpeedCalibration::Phase::Cruise);
  calib.Update(1.0f, 1.0f, kDt, thr, str);
  EXPECT_NEAR(thr, 0.35f, 0.001f);
}

// ── Brake phase ──────────────────────────────────────────────────────────────

TEST_F(SpeedCalibrationTest, BrakePhaseAppliesNegativeThrottle) {
  calib.Start(0.3f, 2.0f);
  float thr = 0, str = 0;
  RunAccelPhase(1.0f, thr, str);
  RunCruisePhase(1.0f, 2.0f, thr, str);
  EXPECT_EQ(calib.GetPhase(), SpeedCalibration::Phase::Brake);
  calib.Update(0.5f, 1.0f, kDt, thr, str);
  EXPECT_LT(thr, 0.0f);  // brake throttle
}

TEST_F(SpeedCalibrationTest, BrakePhaseStopsOnZupt) {
  calib.Start(0.3f, 2.0f);
  float thr = 0, str = 0;
  RunAccelPhase(1.0f, thr, str);
  RunCruisePhase(1.0f, 2.0f, thr, str);
  // ZUPT: accel_mag near 0
  calib.Update(0.0f, 0.01f, kDt, thr, str);
  EXPECT_TRUE(calib.IsFinished());
  EXPECT_EQ(calib.GetPhase(), SpeedCalibration::Phase::Done);
  EXPECT_EQ(thr, 0.0f);
}

TEST_F(SpeedCalibrationTest, BrakePhaseTimesOut) {
  calib.Start(0.3f, 2.0f);
  float thr = 0, str = 0;
  RunAccelPhase(1.0f, thr, str);
  RunCruisePhase(1.0f, 2.0f, thr, str);
  EXPECT_EQ(calib.GetPhase(), SpeedCalibration::Phase::Brake);
  // Run brake phase past timeout (3s)
  const int timeout_steps = static_cast<int>(3.0f / kDt) + 2;
  for (int i = 0; i < timeout_steps; ++i) {
    calib.Update(0.5f, 1.0f, kDt, thr, str);
    if (calib.IsFinished()) break;
  }
  EXPECT_EQ(calib.GetPhase(), SpeedCalibration::Phase::Done);
}

// ── Result computation ───────────────────────────────────────────────────────

TEST_F(SpeedCalibrationTest, ValidResultAfterFullRun) {
  const float kSpeed = 1.5f;
  const float kThr = 0.3f;
  auto result = RunFull(kSpeed, kThr, 3.0f);
  EXPECT_TRUE(result.valid);
  EXPECT_GT(result.samples, 0);
  EXPECT_NEAR(result.mean_speed_ms, kSpeed, 0.01f);
  EXPECT_NEAR(result.speed_gain, kSpeed / kThr, 0.05f);
  EXPECT_NEAR(result.target_throttle, kThr, 0.001f);
}

TEST_F(SpeedCalibrationTest, SpeedGainIsSpeedDividedByThrottle) {
  const float kSpeed = 2.0f;
  const float kThr = 0.4f;
  auto result = RunFull(kSpeed, kThr, 3.0f);
  EXPECT_TRUE(result.valid);
  EXPECT_NEAR(result.speed_gain, kSpeed / kThr, 0.1f);
}

TEST_F(SpeedCalibrationTest, ZeroSpeedProducesInvalidResult) {
  // If EKF reports 0 speed, result should be invalid (mean_speed_ms <= 0.01)
  auto result = RunFull(0.0f, 0.3f, 3.0f);
  EXPECT_FALSE(result.valid);
}

TEST_F(SpeedCalibrationTest, InitialResultIsInvalid) {
  EXPECT_FALSE(calib.GetResult().valid);
  EXPECT_EQ(calib.GetResult().samples, 0);
}

// ── Steering always zero ─────────────────────────────────────────────────────

TEST_F(SpeedCalibrationTest, SteeringAlwaysZero) {
  calib.Start(0.3f, 3.0f);
  float thr = 0, str = 999.0f;
  for (int i = 0; i < 100; ++i) {
    calib.Update(1.0f, 1.0f, kDt, thr, str);
    EXPECT_EQ(str, 0.0f);
  }
}

// ── IsFinished semantics ─────────────────────────────────────────────────────

TEST_F(SpeedCalibrationTest, IsFinishedFalseWhileActive) {
  calib.Start();
  EXPECT_FALSE(calib.IsFinished());
}

TEST_F(SpeedCalibrationTest, IsFinishedTrueAfterDone) {
  RunFull(1.0f);
  EXPECT_TRUE(calib.IsFinished());
}

}  // namespace
}  // namespace rc_vehicle
