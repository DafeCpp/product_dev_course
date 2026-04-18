#include <gtest/gtest.h>

#include <cmath>

#include "test_runner.hpp"

namespace rc_vehicle {
namespace {

// ═══════════════════════════════════════════════════════════════════════════
// Fixture
// ═══════════════════════════════════════════════════════════════════════════

class TestRunnerTest : public ::testing::Test {
 protected:
  using Status = TestRunner::Status;
  using Phase  = TestRunner::Phase;

  TestRunner runner;
  static constexpr float kDt = 0.002f;  // 500 Hz

  void Step(float& throttle, float& steering, float fwd_accel = 0.0f,
            float accel_mag = 1.0f, float gyro_z = 0.0f) {
    runner.Update(fwd_accel, accel_mag, gyro_z, kDt, throttle, steering);
  }

  // 750 steps = exactly 1.5 s, +1 to cross the phase boundary into Cruise
  void RunAccelPhase(float& throttle, float& steering) {
    for (int i = 0; i < 751; ++i) {
      Step(throttle, steering);
    }
  }

  // Run cruise steps using non-ZUPT accel_mag so a phase transition to Brake
  // does not trigger ZUPT within the same helper call.
  void RunCruisePhase(float& throttle, float& steering, int steps) {
    for (int i = 0; i < steps; ++i) {
      runner.Update(0.0f, 0.5f, 0.0f, kDt, throttle, steering);
    }
  }

  // Single step with stopped-vehicle ZUPT conditions → immediate Done
  void RunBrakePhase(float& throttle, float& steering) {
    Step(throttle, steering, 0.0f, 1.0f, 0.0f);
  }

  // Helper: build a minimal valid Straight test
  static TestParams MakeStraight(float duration_sec = 3.0f) {
    TestParams p;
    p.type = TestType::Straight;
    p.target_accel_g = 0.1f;
    p.duration_sec = duration_sec;
    p.steering = 0.0f;
    return p;
  }

  // Helper: Circle test with explicit steering target
  static TestParams MakeCircle(float steer = 0.5f, float duration_sec = 3.0f) {
    TestParams p;
    p.type = TestType::Circle;
    p.target_accel_g = 0.1f;
    p.duration_sec = duration_sec;
    p.steering = steer;
    return p;
  }

  // Helper: Step test with explicit steering target
  static TestParams MakeStep(float steer = 0.8f, float duration_sec = 3.0f) {
    TestParams p;
    p.type = TestType::Step;
    p.target_accel_g = 0.1f;
    p.duration_sec = duration_sec;
    p.steering = steer;
    return p;
  }
};

// ═══════════════════════════════════════════════════════════════════════════
// 1. StartFromIdle
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(TestRunnerTest, StartFromIdle_ReturnsTrue_PhaseIsAccelerate) {
  bool ok = runner.Start(MakeStraight());

  EXPECT_TRUE(ok);
  EXPECT_EQ(runner.GetPhase(), TestRunner::Phase::Accelerate);
  EXPECT_TRUE(runner.IsActive());
  EXPECT_FALSE(runner.IsFinished());
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. CannotStartWhileActive
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(TestRunnerTest, CannotStartWhileActive_ReturnsFalse) {
  runner.Start(MakeStraight());
  ASSERT_EQ(runner.GetPhase(), TestRunner::Phase::Accelerate);

  bool second = runner.Start(MakeStraight());

  EXPECT_FALSE(second);
  EXPECT_EQ(runner.GetPhase(), TestRunner::Phase::Accelerate);
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. StartAfterDone
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(TestRunnerTest, StartAfterDone_ReturnsTrue) {
  runner.Start(MakeStraight());

  float throttle = 0.0f, steering = 0.0f;
  RunAccelPhase(throttle, steering);
  ASSERT_EQ(runner.GetPhase(), TestRunner::Phase::Cruise);

  // 1500 steps = 3.0 s → crosses into Brake
  RunCruisePhase(throttle, steering, 1500);
  ASSERT_EQ(runner.GetPhase(), TestRunner::Phase::Brake);

  RunBrakePhase(throttle, steering);
  ASSERT_EQ(runner.GetPhase(), TestRunner::Phase::Done);

  bool ok = runner.Start(MakeStraight());

  EXPECT_TRUE(ok);
  EXPECT_EQ(runner.GetPhase(), TestRunner::Phase::Accelerate);
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. StartAfterFailed
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(TestRunnerTest, StartAfterFailed_ReturnsTrue) {
  runner.Start(MakeStraight());
  runner.Stop();
  ASSERT_EQ(runner.GetPhase(), TestRunner::Phase::Failed);

  bool ok = runner.Start(MakeStraight());

  EXPECT_TRUE(ok);
  EXPECT_EQ(runner.GetPhase(), TestRunner::Phase::Accelerate);
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. StopWhileActive
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(TestRunnerTest, StopWhileActive_PhaseIsFailed) {
  runner.Start(MakeStraight());
  ASSERT_TRUE(runner.IsActive());

  runner.Stop();

  EXPECT_EQ(runner.GetPhase(), TestRunner::Phase::Failed);
  EXPECT_TRUE(runner.IsFinished());
  EXPECT_FALSE(runner.IsActive());
}

// ═══════════════════════════════════════════════════════════════════════════
// 6. StopWhenIdle
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(TestRunnerTest, StopWhenIdle_NoChange_StillIdle) {
  ASSERT_EQ(runner.GetPhase(), TestRunner::Phase::Idle);

  runner.Stop();

  EXPECT_EQ(runner.GetPhase(), TestRunner::Phase::Idle);
  EXPECT_FALSE(runner.IsActive());
  EXPECT_FALSE(runner.IsFinished());
}

// ═══════════════════════════════════════════════════════════════════════════
// 7. StraightTest_FullCycle
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(TestRunnerTest, StraightTest_FullCycle_DoneWithValidStatus) {
  runner.Start(MakeStraight());

  float throttle = 0.0f, steering = 0.0f;

  // Accelerate → Cruise
  RunAccelPhase(throttle, steering);
  EXPECT_EQ(runner.GetPhase(), TestRunner::Phase::Cruise);

  // Check steering is zero during Cruise for Straight
  EXPECT_FLOAT_EQ(steering, 0.0f);

  // Cruise → Brake (1500 steps = 3.0 s)
  RunCruisePhase(throttle, steering, 1500);
  EXPECT_EQ(runner.GetPhase(), TestRunner::Phase::Brake);

  // Brake → Done (ZUPT)
  RunBrakePhase(throttle, steering);
  EXPECT_EQ(runner.GetPhase(), TestRunner::Phase::Done);
  EXPECT_TRUE(runner.IsFinished());
  EXPECT_FALSE(runner.IsActive());

  Status s = runner.GetStatus();
  EXPECT_TRUE(s.valid);
  EXPECT_EQ(s.phase, TestRunner::Phase::Done);
}

// ═══════════════════════════════════════════════════════════════════════════
// 8. CircleTest_SteeringApplied
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(TestRunnerTest, CircleTest_SteeringApplied_DuringCruise) {
  const float kTargetSteer = 0.7f;
  runner.Start(MakeCircle(kTargetSteer));

  float throttle = 0.0f, steering = 0.0f;
  RunAccelPhase(throttle, steering);
  ASSERT_EQ(runner.GetPhase(), TestRunner::Phase::Cruise);

  // Take a fresh cruise step and check steering equals target
  Step(throttle, steering);

  EXPECT_FLOAT_EQ(steering, kTargetSteer);
}

// ═══════════════════════════════════════════════════════════════════════════
// 9. StepTest_PhaseSequence
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(TestRunnerTest, StepTest_PhaseSequence_AccelCruiseStepExecBrakeDone) {
  const float kTargetSteer = 0.8f;
  runner.Start(MakeStep(kTargetSteer));

  float throttle = 0.0f, steering = 0.0f;

  // Accelerate → Cruise
  RunAccelPhase(throttle, steering);
  EXPECT_EQ(runner.GetPhase(), TestRunner::Phase::Cruise);

  // During Cruise (settle) steering must be zero
  Step(throttle, steering);
  EXPECT_FLOAT_EQ(steering, 0.0f);

  // 500 steps (+ 1 initial Step above) = 501 total → crosses into StepExec
  RunCruisePhase(throttle, steering, 500);
  EXPECT_EQ(runner.GetPhase(), TestRunner::Phase::StepExec);

  // During StepExec steering must equal target
  Step(throttle, steering);
  EXPECT_FLOAT_EQ(steering, kTargetSteer);

  // StepExec for 3.0 s (default duration_sec), 1500 steps → Brake
  RunCruisePhase(throttle, steering, 1500);
  EXPECT_EQ(runner.GetPhase(), TestRunner::Phase::Brake);

  // Brake → Done via ZUPT
  RunBrakePhase(throttle, steering);
  EXPECT_EQ(runner.GetPhase(), TestRunner::Phase::Done);
}

// ═══════════════════════════════════════════════════════════════════════════
// 10. ThrottlePositiveDuringAccel
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(TestRunnerTest, ThrottlePositiveDuringAccel_WhenFwdAccelIsZero) {
  runner.Start(MakeStraight());
  ASSERT_EQ(runner.GetPhase(), TestRunner::Phase::Accelerate);

  float throttle = 0.0f, steering = 0.0f;

  // Feed zero forward accel → PID error is positive → throttle positive
  Step(throttle, steering, 0.0f);

  EXPECT_GT(throttle, 0.0f);
  EXPECT_FLOAT_EQ(steering, 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// 11. CruiseHoldsThrottle
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(TestRunnerTest, CruiseHoldsThrottle_PositiveAndConstant) {
  runner.Start(MakeStraight());

  float throttle = 0.0f, steering = 0.0f;
  RunAccelPhase(throttle, steering);
  ASSERT_EQ(runner.GetPhase(), TestRunner::Phase::Cruise);

  float first_cruise_throttle = throttle;
  EXPECT_GT(first_cruise_throttle, 0.0f);

  // Multiple cruise steps — throttle should stay constant
  for (int i = 0; i < 10; ++i) {
    Step(throttle, steering);
    EXPECT_FLOAT_EQ(throttle, first_cruise_throttle);
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// 12. BrakeZupt
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(TestRunnerTest, BrakeZupt_ImmediatelyDone_WhenVehicleStopped) {
  runner.Start(MakeStraight());

  float throttle = 0.0f, steering = 0.0f;
  RunAccelPhase(throttle, steering);
  RunCruisePhase(throttle, steering, 1500);
  ASSERT_EQ(runner.GetPhase(), TestRunner::Phase::Brake);

  // accel_mag=1.0 → |1.0-1.0|=0.0 < 0.05, gyro_z=0.0 < 3.0 → ZUPT
  Step(throttle, steering, 0.0f, 1.0f, 0.0f);

  EXPECT_EQ(runner.GetPhase(), TestRunner::Phase::Done);
  EXPECT_FLOAT_EQ(throttle, 0.0f);
  EXPECT_FLOAT_EQ(steering, 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// 13. BrakeTimeout
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(TestRunnerTest, BrakeTimeout_Done_After3Seconds) {
  runner.Start(MakeStraight());

  float throttle = 0.0f, steering = 0.0f;
  RunAccelPhase(throttle, steering);
  RunCruisePhase(throttle, steering, 1500);
  ASSERT_EQ(runner.GetPhase(), TestRunner::Phase::Brake);

  // Feed non-ZUPT data: accel_mag=0.0 → |0.0-1.0|=1.0 > 0.05 (no ZUPT)
  // 1499 steps = 2.998 s — still Brake
  for (int i = 0; i < 1499; ++i) {
    Step(throttle, steering, 0.0f, 0.0f, 0.0f);
    ASSERT_EQ(runner.GetPhase(), TestRunner::Phase::Brake) << "Early Done at step " << i;
  }

  // Step 1500 crosses 3.0 s timeout → Done
  Step(throttle, steering, 0.0f, 0.0f, 0.0f);
  EXPECT_EQ(runner.GetPhase(), TestRunner::Phase::Done);
}

// ═══════════════════════════════════════════════════════════════════════════
// 14. TestMarkerActive
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(TestRunnerTest, TestMarkerActive_ReturnsTestTypeAsUint8) {
  runner.Start(MakeCircle());
  ASSERT_TRUE(runner.IsActive());

  EXPECT_EQ(runner.GetTestMarker(), static_cast<uint8_t>(TestType::Circle));
  EXPECT_EQ(runner.GetTestMarker(), 2u);
}

// ═══════════════════════════════════════════════════════════════════════════
// 15. TestMarkerInactive
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(TestRunnerTest, TestMarkerInactive_ReturnsZero_WhenDone) {
  // GetTestMarker uses IsActive() check → 0 when phase is Done
  runner.Start(MakeStep());

  float throttle = 0.0f, steering = 0.0f;
  RunAccelPhase(throttle, steering);
  // Settle cruise for Step: 501 steps to cross 1.0 s boundary
  RunCruisePhase(throttle, steering, 501);
  ASSERT_EQ(runner.GetPhase(), TestRunner::Phase::StepExec);
  // StepExec: 1500 steps = 3.0 s
  RunCruisePhase(throttle, steering, 1500);
  ASSERT_EQ(runner.GetPhase(), TestRunner::Phase::Brake);
  // Brake → Done
  RunBrakePhase(throttle, steering);
  ASSERT_EQ(runner.GetPhase(), TestRunner::Phase::Done);
  ASSERT_FALSE(runner.IsActive());

  EXPECT_EQ(runner.GetTestMarker(), 0u);
}

TEST_F(TestRunnerTest, TestMarkerInactive_ReturnsZero_WhenIdle) {
  ASSERT_EQ(runner.GetPhase(), TestRunner::Phase::Idle);
  ASSERT_FALSE(runner.IsActive());

  EXPECT_EQ(runner.GetTestMarker(), 0u);
}

TEST_F(TestRunnerTest, TestMarkerInactive_ReturnsZero_AfterReset) {
  runner.Start(MakeCircle());
  runner.Reset();
  ASSERT_EQ(runner.GetPhase(), TestRunner::Phase::Idle);
  ASSERT_FALSE(runner.IsActive());

  EXPECT_EQ(runner.GetTestMarker(), 0u);
}

// ═══════════════════════════════════════════════════════════════════════════
// 16. Reset
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(TestRunnerTest, Reset_PhaseIsIdle_AllTimersZero) {
  runner.Start(MakeStraight());

  float throttle = 0.0f, steering = 0.0f;
  RunAccelPhase(throttle, steering);
  ASSERT_NE(runner.GetPhase(), TestRunner::Phase::Idle);

  runner.Reset();

  EXPECT_EQ(runner.GetPhase(), TestRunner::Phase::Idle);
  EXPECT_FALSE(runner.IsActive());
  EXPECT_FALSE(runner.IsFinished());

  Status s = runner.GetStatus();
  EXPECT_FLOAT_EQ(s.elapsed_sec, 0.0f);
  EXPECT_FLOAT_EQ(s.phase_elapsed_sec, 0.0f);
  EXPECT_FALSE(s.valid);
}

// ═══════════════════════════════════════════════════════════════════════════
// 17. ZeroDtNoop
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(TestRunnerTest, ZeroDt_IsNoop_PhaseAndTimersUnchanged) {
  runner.Start(MakeStraight());
  ASSERT_EQ(runner.GetPhase(), TestRunner::Phase::Accelerate);

  Status before = runner.GetStatus();

  float throttle = 99.0f, steering = 99.0f;
  runner.Update(0.0f, 1.0f, 0.0f, 0.0f, throttle, steering);

  Status after = runner.GetStatus();

  EXPECT_EQ(after.phase, before.phase);
  EXPECT_FLOAT_EQ(after.elapsed_sec, before.elapsed_sec);
  EXPECT_FLOAT_EQ(after.phase_elapsed_sec, before.phase_elapsed_sec);

  // Outputs must be zeroed (early return branch still zeros them)
  EXPECT_FLOAT_EQ(throttle, 0.0f);
  EXPECT_FLOAT_EQ(steering, 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// 18. IdleOutputZero
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(TestRunnerTest, IdleOutputZero_ThrottleAndSteeringAreZero) {
  ASSERT_EQ(runner.GetPhase(), TestRunner::Phase::Idle);

  float throttle = 1.0f, steering = 1.0f;
  runner.Update(0.5f, 1.0f, 5.0f, kDt, throttle, steering);

  EXPECT_FLOAT_EQ(throttle, 0.0f);
  EXPECT_FLOAT_EQ(steering, 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// 19. ParamsClamping
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(TestRunnerTest, ParamsClamping_ExtremeValues_TestStillRuns) {
  TestParams p;
  p.type = TestType::Straight;
  p.target_accel_g = 999.0f;   // clamped to 0.3
  p.duration_sec = -5.0f;      // clamped to 1.0
  p.steering = 50.0f;          // clamped to 1.0

  bool ok = runner.Start(p);
  EXPECT_TRUE(ok);
  EXPECT_EQ(runner.GetPhase(), TestRunner::Phase::Accelerate);

  float throttle = 0.0f, steering = 0.0f;
  // Accel phase 750+1 steps
  RunAccelPhase(throttle, steering);
  EXPECT_EQ(runner.GetPhase(), TestRunner::Phase::Cruise);

  // duration_sec clamped to 1.0 → 501 steps to cross into Brake
  RunCruisePhase(throttle, steering, 501);
  EXPECT_EQ(runner.GetPhase(), TestRunner::Phase::Brake);
}

TEST_F(TestRunnerTest, ParamsClamping_BelowMin_TargetAccelClamped) {
  TestParams p;
  p.type = TestType::Straight;
  p.target_accel_g = -1.0f;  // below 0.02 → clamped to 0.02
  p.duration_sec = 1.0f;
  p.steering = 0.0f;

  bool ok = runner.Start(p);
  EXPECT_TRUE(ok);
  EXPECT_EQ(runner.GetPhase(), TestRunner::Phase::Accelerate);

  // PID should still produce positive throttle (error = 0.02 - 0.0 > 0)
  float throttle = 0.0f, steering = 0.0f;
  Step(throttle, steering, 0.0f);
  EXPECT_GT(throttle, 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// 20. GetStatus
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(TestRunnerTest, GetStatus_IdleState_DefaultValues) {
  Status s = runner.GetStatus();

  EXPECT_EQ(s.phase, TestRunner::Phase::Idle);
  EXPECT_EQ(s.type, TestType::Straight);
  EXPECT_FLOAT_EQ(s.elapsed_sec, 0.0f);
  EXPECT_FLOAT_EQ(s.phase_elapsed_sec, 0.0f);
  EXPECT_FALSE(s.valid);
}

TEST_F(TestRunnerTest, GetStatus_DuringAccel_ElapsedTimeIncreases) {
  runner.Start(MakeCircle());

  float throttle = 0.0f, steering = 0.0f;
  // Run 100 steps = 0.2 s
  for (int i = 0; i < 100; ++i) {
    Step(throttle, steering);
  }

  Status s = runner.GetStatus();

  EXPECT_EQ(s.phase, TestRunner::Phase::Accelerate);
  EXPECT_EQ(s.type, TestType::Circle);
  EXPECT_NEAR(s.elapsed_sec, 0.2f, 0.001f);
  EXPECT_NEAR(s.phase_elapsed_sec, 0.2f, 0.001f);
  EXPECT_FALSE(s.valid);
}

TEST_F(TestRunnerTest, GetStatus_AfterDone_ValidIsTrue) {
  runner.Start(MakeStraight());

  float throttle = 0.0f, steering = 0.0f;
  RunAccelPhase(throttle, steering);
  RunCruisePhase(throttle, steering, 1500);
  RunBrakePhase(throttle, steering);
  ASSERT_EQ(runner.GetPhase(), TestRunner::Phase::Done);

  Status s = runner.GetStatus();

  EXPECT_TRUE(s.valid);
  EXPECT_EQ(s.phase, TestRunner::Phase::Done);
  EXPECT_EQ(s.type, TestType::Straight);
}

TEST_F(TestRunnerTest, GetStatus_AfterFailed_ValidIsFalse) {
  runner.Start(MakeStraight());
  runner.Stop();
  ASSERT_EQ(runner.GetPhase(), TestRunner::Phase::Failed);

  Status s = runner.GetStatus();

  EXPECT_FALSE(s.valid);
  EXPECT_EQ(s.phase, TestRunner::Phase::Failed);
}

TEST_F(TestRunnerTest, GetStatus_PhaseElapsedResetOnTransition) {
  runner.Start(MakeStraight());

  float throttle = 0.0f, steering = 0.0f;
  // Run full accel phase (751 steps)
  RunAccelPhase(throttle, steering);
  ASSERT_EQ(runner.GetPhase(), TestRunner::Phase::Cruise);

  Status s = runner.GetStatus();

  // After transition to Cruise, phase_elapsed starts from the step that
  // crossed the boundary — it should be much less than total elapsed
  EXPECT_LT(s.phase_elapsed_sec, s.elapsed_sec);
  EXPECT_GE(s.phase_elapsed_sec, 0.0f);
}

}  // namespace
}  // namespace rc_vehicle
