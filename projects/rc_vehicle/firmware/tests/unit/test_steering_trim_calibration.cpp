#include <gtest/gtest.h>

#include <cmath>

#include "steering_trim_calibration.hpp"

namespace rc_vehicle {
namespace {

class SteeringTrimCalibrationTest : public ::testing::Test {
 protected:
  SteeringTrimCalibration calib;
  static constexpr float kDt = 0.002f;  // 500 Hz

  // Simulate one step with given yaw rate
  void Step(float yaw_rate_dps, float& throttle, float& steering,
            float fwd_accel = 0.1f, float accel_mag = 1.0f) {
    calib.Update(fwd_accel, accel_mag, yaw_rate_dps, kDt, throttle, steering);
  }

  // Run through accelerate phase (fwd_accel=0 → PID produces throttle)
  void RunAccelPhase(float& throttle, float& steering) {
    for (int i = 0; i < 751; ++i) {  // 1.5s / 0.002s = 750, +1 to cross boundary
      Step(0.0f, throttle, steering, 0.0f, 1.0f);
    }
  }

  // Run through cruise phase collecting yaw rate
  void RunCruisePhase(float yaw_rate_dps, float& throttle, float& steering) {
    for (int i = 0; i < 2001; ++i) {  // 4.0s / 0.002s = 2000, +1 to cross
      Step(yaw_rate_dps, throttle, steering, 0.0f, 1.0f);
    }
  }

  // Run through brake phase (stopped immediately)
  void RunBrakePhase(float& throttle, float& steering) {
    // Simulate stopped: accel_mag ≈ 1g, gyro_z ≈ 0
    Step(0.0f, throttle, steering, 0.0f, 1.0f);
  }

  // Run complete calibration with given yaw rate
  SteeringTrimCalibration::Result RunFullCalibration(
      float yaw_rate_dps, float current_trim = 0.0f,
      float sensitivity = 180.0f) {
    calib.Start(0.1f, current_trim, sensitivity);
    float thr = 0, str = 0;
    RunAccelPhase(thr, str);
    RunCruisePhase(yaw_rate_dps, thr, str);
    RunBrakePhase(thr, str);
    return calib.GetResult();
  }
};

TEST_F(SteeringTrimCalibrationTest, StartFromIdle) {
  EXPECT_TRUE(calib.Start());
  EXPECT_TRUE(calib.IsActive());
  EXPECT_EQ(calib.GetPhase(), SteeringTrimCalibration::Phase::Accelerate);
}

TEST_F(SteeringTrimCalibrationTest, CannotStartWhileActive) {
  EXPECT_TRUE(calib.Start());
  EXPECT_FALSE(calib.Start());  // Already active
}

TEST_F(SteeringTrimCalibrationTest, CanRestartAfterDone) {
  auto result = RunFullCalibration(0.0f);
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(calib.Start());  // Can restart after Done
}

TEST_F(SteeringTrimCalibrationTest, CanRestartAfterFailed) {
  calib.Start();
  calib.Stop();
  EXPECT_TRUE(calib.Start());  // Can restart after Failed
}

TEST_F(SteeringTrimCalibrationTest, StopDuringCalibration) {
  calib.Start();
  EXPECT_TRUE(calib.IsActive());
  calib.Stop();
  EXPECT_EQ(calib.GetPhase(), SteeringTrimCalibration::Phase::Failed);
  EXPECT_FALSE(calib.GetResult().valid);
}

TEST_F(SteeringTrimCalibrationTest, PhaseTransitions) {
  calib.Start();
  float thr = 0, str = 0;

  // Accelerate phase
  EXPECT_EQ(calib.GetPhase(), SteeringTrimCalibration::Phase::Accelerate);

  RunAccelPhase(thr, str);

  // Should be in Cruise now
  EXPECT_EQ(calib.GetPhase(), SteeringTrimCalibration::Phase::Cruise);

  // Run cruise but leave 1 step before transition to see Brake
  for (int i = 0; i < 2000; ++i) {  // 4.0s / 0.002s = 2000 (exactly at boundary)
    Step(0.0f, thr, str, 0.0f, 1.0f);
  }

  // Should be in Brake now
  EXPECT_EQ(calib.GetPhase(), SteeringTrimCalibration::Phase::Brake);

  // Brake: simulate stopped (accel_mag ≈ 1g, gyro_z ≈ 0)
  RunBrakePhase(thr, str);

  // Should be Done
  EXPECT_EQ(calib.GetPhase(), SteeringTrimCalibration::Phase::Done);
}

TEST_F(SteeringTrimCalibrationTest, SteeringIsZeroDuringCalibration) {
  calib.Start();
  float thr = 0, str = 999.0f;

  // All phases should output steering = 0
  for (int i = 0; i < 3000; ++i) {
    Step(5.0f, thr, str, 0.1f, 1.0f);
    EXPECT_FLOAT_EQ(str, 0.0f) << "Step " << i;
  }
}

TEST_F(SteeringTrimCalibrationTest, ThrottlePositiveDuringAccelAndCruise) {
  calib.Start();
  float thr = 0, str = 0;

  // Accelerate: fwd_accel=0 → error=0.1g → PID should produce positive throttle
  for (int i = 0; i < 100; ++i) {
    Step(0.0f, thr, str, 0.0f, 1.0f);
  }
  EXPECT_GT(thr, 0.0f);

  // Run through rest of accel phase
  RunAccelPhase(thr, str);

  // Cruise: should maintain throttle (cruise_throttle captured from accel)
  Step(0.0f, thr, str, 0.0f, 1.0f);
  EXPECT_GT(thr, 0.0f);
}

TEST_F(SteeringTrimCalibrationTest, ZeroYawRateGivesCurrentTrim) {
  // No drift → trim should stay at current value
  auto result = RunFullCalibration(0.0f, 0.05f, 180.0f);
  EXPECT_TRUE(result.valid);
  EXPECT_NEAR(result.trim, 0.05f, 0.001f);
  EXPECT_NEAR(result.mean_yaw_rate, 0.0f, 0.1f);
}

TEST_F(SteeringTrimCalibrationTest, PositiveYawRateGivesNegativeTrimCorrection) {
  // Drifting right (positive yaw_rate) → need negative trim correction
  // sensitivity = 180 dps, yaw_rate = 5 dps → correction = -5/180 ≈ -0.028
  auto result = RunFullCalibration(5.0f, 0.0f, 180.0f);
  EXPECT_TRUE(result.valid);
  EXPECT_NEAR(result.trim, -5.0f / 180.0f, 0.002f);
  EXPECT_GT(result.samples, 500);
}

TEST_F(SteeringTrimCalibrationTest, NegativeYawRateGivesPositiveTrimCorrection) {
  // Drifting left (negative yaw_rate) → need positive trim correction
  auto result = RunFullCalibration(-3.0f, 0.0f, 180.0f);
  EXPECT_TRUE(result.valid);
  EXPECT_NEAR(result.trim, 3.0f / 180.0f, 0.002f);
}

TEST_F(SteeringTrimCalibrationTest, TrimAddsToCurrent) {
  // Current trim = 0.03, drift = 2 dps → correction = -2/180 ≈ -0.011
  // New trim = 0.03 + (-0.011) ≈ 0.019
  auto result = RunFullCalibration(2.0f, 0.03f, 180.0f);
  EXPECT_TRUE(result.valid);
  EXPECT_NEAR(result.trim, 0.03f - 2.0f / 180.0f, 0.002f);
}

TEST_F(SteeringTrimCalibrationTest, TrimClampedToRange) {
  // Large drift with high sensitivity → trim would be large, but clamped to ±0.1
  auto result = RunFullCalibration(25.0f, 0.0f, 180.0f);
  EXPECT_TRUE(result.valid);
  EXPECT_GE(result.trim, -0.1f);
  EXPECT_LE(result.trim, 0.1f);
}

TEST_F(SteeringTrimCalibrationTest, ExcessiveYawRateFails) {
  // yaw_rate > 30 dps → too much drift, trim won't help
  auto result = RunFullCalibration(35.0f, 0.0f, 180.0f);
  EXPECT_FALSE(result.valid);
  EXPECT_EQ(calib.GetPhase(), SteeringTrimCalibration::Phase::Failed);
}

TEST_F(SteeringTrimCalibrationTest, SensitivityAffectsResult) {
  // Lower sensitivity → larger trim correction for same yaw_rate
  auto result_high = RunFullCalibration(5.0f, 0.0f, 360.0f);
  calib.Reset();
  auto result_low = RunFullCalibration(5.0f, 0.0f, 90.0f);

  EXPECT_TRUE(result_high.valid);
  EXPECT_TRUE(result_low.valid);
  // Lower sensitivity → larger |trim|
  EXPECT_GT(std::abs(result_low.trim), std::abs(result_high.trim));
}

TEST_F(SteeringTrimCalibrationTest, ResetClearsState) {
  calib.Start();
  float thr = 0, str = 0;
  RunAccelPhase(thr, str);
  calib.Reset();

  EXPECT_EQ(calib.GetPhase(), SteeringTrimCalibration::Phase::Idle);
  EXPECT_FALSE(calib.IsActive());
  EXPECT_FALSE(calib.GetResult().valid);
}

TEST_F(SteeringTrimCalibrationTest, UpdateWithZeroDtIsNoop) {
  calib.Start();
  float thr = 0, str = 0;
  auto phase_before = calib.GetPhase();
  calib.Update(0.1f, 1.0f, 0.0f, 0.0f, thr, str);
  EXPECT_EQ(calib.GetPhase(), phase_before);
  EXPECT_FLOAT_EQ(thr, 0.0f);
}

TEST_F(SteeringTrimCalibrationTest, UpdateWhenIdleOutputsZero) {
  float thr = 999.0f, str = 999.0f;
  calib.Update(0.1f, 1.0f, 5.0f, kDt, thr, str);
  EXPECT_FLOAT_EQ(thr, 0.0f);
  EXPECT_FLOAT_EQ(str, 0.0f);
}

TEST_F(SteeringTrimCalibrationTest, BrakeTimeoutWorks) {
  calib.Start();
  float thr = 0, str = 0;
  RunAccelPhase(thr, str);
  RunCruisePhase(1.0f, thr, str);

  // Brake phase: vehicle never stops (accel_mag != 1.0)
  for (int i = 0; i < 1501; ++i) {  // 3.0s / 0.002s = 1500, +1 for boundary
    calib.Update(0.0f, 0.5f, 5.0f, kDt, thr, str);  // Not stopped
  }

  // Should have timed out and computed result
  EXPECT_TRUE(calib.IsFinished());
}

TEST_F(SteeringTrimCalibrationTest, CruiseSkipsFirst500ms) {
  // Verify that samples during first 0.5s of cruise are not counted
  calib.Start(0.1f, 0.0f, 180.0f);
  float thr = 0, str = 0;
  RunAccelPhase(thr, str);

  // First 0.5s of cruise with huge yaw rate (251 steps to cross 0.5s)
  for (int i = 0; i < 251; ++i) {
    Step(100.0f, thr, str, 0.0f, 1.0f);  // Should be ignored
  }
  // Rest of cruise with 0 yaw rate (need total cruise >= 4.0s + 1 step)
  for (int i = 0; i < 1751; ++i) {
    Step(0.0f, thr, str, 0.0f, 1.0f);
  }

  RunBrakePhase(thr, str);

  auto result = calib.GetResult();
  EXPECT_TRUE(result.valid);
  // If first 0.5s samples were counted, mean would be much higher
  EXPECT_NEAR(result.mean_yaw_rate, 0.0f, 0.5f);
}

TEST_F(SteeringTrimCalibrationTest, MinSensitivityClamp) {
  // Sensitivity below 10 should be clamped to 10
  auto result = RunFullCalibration(5.0f, 0.0f, 1.0f);
  EXPECT_TRUE(result.valid);
  // trim = -5/10 = -0.1 (clamped)
  EXPECT_NEAR(result.trim, -0.1f, 0.01f);
}

}  // namespace
}  // namespace rc_vehicle
