#include <gtest/gtest.h>

#include "auto_drive_coordinator.hpp"
#include "telemetry_event_log.hpp"

using namespace rc_vehicle;

// ══════════════════════════════════════════════════════════════════════════════
// Helpers
// ══════════════════════════════════════════════════════════════════════════════

/** Minimal valid input with no RC signal and IMU enabled. */
static AutoDriveInput IdleInput() {
  AutoDriveInput in;
  in.fwd_accel   = 0.0f;
  in.accel_mag   = 1.0f;
  in.gyro_z      = 0.0f;
  in.cal_ax      = 0.0f;
  in.cal_ay      = 0.0f;
  in.dt_sec      = 0.002f;
  in.speed_ms    = 0.0f;
  in.ts_ms       = 0;
  in.rc_active   = false;
  in.imu_enabled = true;
  return in;
}

/** Input that signals active RC override. */
static AutoDriveInput RcActiveInput() {
  AutoDriveInput in = IdleInput();
  in.rc_active = true;
  return in;
}

/** Default TestParams (Straight, safe values). */
static TestParams DefaultTestParams() {
  TestParams p;
  p.type           = TestType::Straight;
  p.target_accel_g = 0.1f;
  p.duration_sec   = 3.0f;
  p.steering       = 0.0f;
  return p;
}

// ══════════════════════════════════════════════════════════════════════════════
// Initial state
// ══════════════════════════════════════════════════════════════════════════════

TEST(AutoDriveCoordinatorTest, InitialState_NoProcedureActive) {
  AutoDriveCoordinator adc;
  EXPECT_FALSE(adc.IsAnyActive());
  EXPECT_FALSE(adc.IsTrimCalibActive());
  EXPECT_FALSE(adc.IsComCalibActive());
  EXPECT_FALSE(adc.IsTestActive());
  EXPECT_FALSE(adc.IsSpeedCalibActive());
}

TEST(AutoDriveCoordinatorTest, InitialState_UpdateReturnsInactiveOutput) {
  AutoDriveCoordinator adc;
  AutoDriveOutput out = adc.Update(IdleInput());
  EXPECT_FALSE(out.active);
  EXPECT_FLOAT_EQ(out.throttle, 0.0f);
  EXPECT_FLOAT_EQ(out.steering, 0.0f);
  EXPECT_FALSE(out.trim_completed);
  EXPECT_FALSE(out.com_completed);
  EXPECT_FALSE(out.speed_cal_completed);
}

TEST(AutoDriveCoordinatorTest, InitialState_TestMarkerIsZero) {
  AutoDriveCoordinator adc;
  EXPECT_EQ(adc.GetTestMarker(), 0);
}

// ══════════════════════════════════════════════════════════════════════════════
// StartTrimCalib
// ══════════════════════════════════════════════════════════════════════════════

TEST(AutoDriveCoordinatorTest, StartTrimCalib_SucceedsWhenNothingActive) {
  AutoDriveCoordinator adc;
  bool ok = adc.StartTrimCalib(0.1f, 0.0f, 180.0f);
  EXPECT_TRUE(ok);
  EXPECT_TRUE(adc.IsTrimCalibActive());
  EXPECT_TRUE(adc.IsAnyActive());
}

TEST(AutoDriveCoordinatorTest, StartTrimCalib_ProducesActiveOutput) {
  AutoDriveCoordinator adc;
  adc.StartTrimCalib(0.1f, 0.0f, 180.0f);
  AutoDriveOutput out = adc.Update(IdleInput());
  EXPECT_TRUE(out.active);
}

// ══════════════════════════════════════════════════════════════════════════════
// StartComCalib
// ══════════════════════════════════════════════════════════════════════════════

TEST(AutoDriveCoordinatorTest, StartComCalib_SucceedsWhenNothingActive) {
  AutoDriveCoordinator adc;
  bool ok = adc.StartComCalib(0.1f, 0.5f, 5.0f, nullptr);
  EXPECT_TRUE(ok);
  EXPECT_TRUE(adc.IsComCalibActive());
  EXPECT_TRUE(adc.IsAnyActive());
}

TEST(AutoDriveCoordinatorTest, StartComCalib_ProducesActiveOutput) {
  AutoDriveCoordinator adc;
  adc.StartComCalib(0.1f, 0.5f, 5.0f, nullptr);
  AutoDriveOutput out = adc.Update(IdleInput());
  EXPECT_TRUE(out.active);
}

// ══════════════════════════════════════════════════════════════════════════════
// StartTest
// ══════════════════════════════════════════════════════════════════════════════

TEST(AutoDriveCoordinatorTest, StartTest_SucceedsWhenNothingActive) {
  AutoDriveCoordinator adc;
  bool ok = adc.StartTest(DefaultTestParams());
  EXPECT_TRUE(ok);
  EXPECT_TRUE(adc.IsTestActive());
  EXPECT_TRUE(adc.IsAnyActive());
}

TEST(AutoDriveCoordinatorTest, StartTest_ProducesActiveOutput) {
  AutoDriveCoordinator adc;
  adc.StartTest(DefaultTestParams());
  AutoDriveOutput out = adc.Update(IdleInput());
  EXPECT_TRUE(out.active);
}

TEST(AutoDriveCoordinatorTest, StartTest_MarkerReflectsTestType) {
  AutoDriveCoordinator adc;
  TestParams p = DefaultTestParams();
  p.type = TestType::Circle;
  adc.StartTest(p);
  EXPECT_EQ(adc.GetTestMarker(), static_cast<float>(TestType::Circle));
}

// ══════════════════════════════════════════════════════════════════════════════
// StartSpeedCalib
// ══════════════════════════════════════════════════════════════════════════════

TEST(AutoDriveCoordinatorTest, StartSpeedCalib_SucceedsWithValidParams) {
  AutoDriveCoordinator adc;
  bool ok = adc.StartSpeedCalib(0.3f, 3.0f);
  EXPECT_TRUE(ok);
  EXPECT_TRUE(adc.IsSpeedCalibActive());
  EXPECT_TRUE(adc.IsAnyActive());
}

TEST(AutoDriveCoordinatorTest, StartSpeedCalib_ProducesActiveOutput) {
  AutoDriveCoordinator adc;
  adc.StartSpeedCalib(0.3f, 3.0f);
  AutoDriveOutput out = adc.Update(IdleInput());
  EXPECT_TRUE(out.active);
}

// SpeedCalibration::Start() rejects throttle outside [0.1, 0.8].
// AutoDriveCoordinator propagates that rejection even when nothing else is
// running.
TEST(AutoDriveCoordinatorTest,
     StartSpeedCalib_RejectsOutOfRangeThrottle_WhenNothingActive) {
  AutoDriveCoordinator adc;
  EXPECT_FALSE(adc.StartSpeedCalib(0.0f, 3.0f));   // below min
  EXPECT_FALSE(adc.StartSpeedCalib(0.9f, 3.0f));   // above max
  EXPECT_FALSE(adc.IsSpeedCalibActive());
  EXPECT_FALSE(adc.IsAnyActive());
}

TEST(AutoDriveCoordinatorTest,
     StartSpeedCalib_RejectsOutOfRangeDuration_WhenNothingActive) {
  AutoDriveCoordinator adc;
  EXPECT_FALSE(adc.StartSpeedCalib(0.3f, 0.5f));   // below min
  EXPECT_FALSE(adc.StartSpeedCalib(0.3f, 11.0f));  // above max
  EXPECT_FALSE(adc.IsSpeedCalibActive());
}

// ══════════════════════════════════════════════════════════════════════════════
// Mutual exclusion — starting a second procedure while one is active
//
// Policy (from auto_drive_coordinator.cpp):
//   All StartX() call IsAnyActive() first and return false immediately if
//   anything is active. The first procedure is NOT stopped.
// ══════════════════════════════════════════════════════════════════════════════

TEST(AutoDriveCoordinatorTest,
     MutualExclusion_StartComCalib_FailsWhileTrimActive) {
  AutoDriveCoordinator adc;
  ASSERT_TRUE(adc.StartTrimCalib(0.1f, 0.0f, 180.0f));

  bool second = adc.StartComCalib(0.1f, 0.5f, 5.0f, nullptr);
  EXPECT_FALSE(second);

  // First procedure must still be running — not silently stopped
  EXPECT_TRUE(adc.IsTrimCalibActive());
  EXPECT_FALSE(adc.IsComCalibActive());
}

TEST(AutoDriveCoordinatorTest,
     MutualExclusion_StartTest_FailsWhileTrimActive) {
  AutoDriveCoordinator adc;
  ASSERT_TRUE(adc.StartTrimCalib(0.1f, 0.0f, 180.0f));

  bool second = adc.StartTest(DefaultTestParams());
  EXPECT_FALSE(second);

  EXPECT_TRUE(adc.IsTrimCalibActive());
  EXPECT_FALSE(adc.IsTestActive());
}

TEST(AutoDriveCoordinatorTest,
     MutualExclusion_StartSpeedCalib_FailsWhileTrimActive) {
  AutoDriveCoordinator adc;
  ASSERT_TRUE(adc.StartTrimCalib(0.1f, 0.0f, 180.0f));

  bool second = adc.StartSpeedCalib(0.3f, 3.0f);
  EXPECT_FALSE(second);

  EXPECT_TRUE(adc.IsTrimCalibActive());
  EXPECT_FALSE(adc.IsSpeedCalibActive());
}

TEST(AutoDriveCoordinatorTest,
     MutualExclusion_StartTrimCalib_FailsWhileTestActive) {
  AutoDriveCoordinator adc;
  ASSERT_TRUE(adc.StartTest(DefaultTestParams()));

  bool second = adc.StartTrimCalib(0.1f, 0.0f, 180.0f);
  EXPECT_FALSE(second);

  EXPECT_TRUE(adc.IsTestActive());
  EXPECT_FALSE(adc.IsTrimCalibActive());
}

TEST(AutoDriveCoordinatorTest,
     MutualExclusion_StartTrimCalib_FailsWhileSpeedCalibActive) {
  AutoDriveCoordinator adc;
  ASSERT_TRUE(adc.StartSpeedCalib(0.3f, 3.0f));

  bool second = adc.StartTrimCalib(0.1f, 0.0f, 180.0f);
  EXPECT_FALSE(second);

  EXPECT_TRUE(adc.IsSpeedCalibActive());
  EXPECT_FALSE(adc.IsTrimCalibActive());
}

// ══════════════════════════════════════════════════════════════════════════════
// StopX on inactive procedure is a no-op
// ══════════════════════════════════════════════════════════════════════════════

TEST(AutoDriveCoordinatorTest, StopTrimCalib_WhenInactive_IsNoOp) {
  AutoDriveCoordinator adc;
  // Should not crash, and IsAnyActive() must stay false
  adc.StopTrimCalib();
  EXPECT_FALSE(adc.IsAnyActive());
}

TEST(AutoDriveCoordinatorTest, StopComCalib_WhenInactive_IsNoOp) {
  AutoDriveCoordinator adc;
  adc.StopComCalib();
  EXPECT_FALSE(adc.IsAnyActive());
}

TEST(AutoDriveCoordinatorTest, StopTest_WhenInactive_IsNoOp) {
  AutoDriveCoordinator adc;
  adc.StopTest();
  EXPECT_FALSE(adc.IsAnyActive());
}

TEST(AutoDriveCoordinatorTest, StopSpeedCalib_WhenInactive_IsNoOp) {
  AutoDriveCoordinator adc;
  adc.StopSpeedCalib();
  EXPECT_FALSE(adc.IsAnyActive());
}

// ══════════════════════════════════════════════════════════════════════════════
// StopX terminates the correct running procedure
// ══════════════════════════════════════════════════════════════════════════════

TEST(AutoDriveCoordinatorTest, StopTrimCalib_TerminatesActiveProcedure) {
  AutoDriveCoordinator adc;
  ASSERT_TRUE(adc.StartTrimCalib(0.1f, 0.0f, 180.0f));
  adc.StopTrimCalib();
  EXPECT_FALSE(adc.IsTrimCalibActive());
  EXPECT_FALSE(adc.IsAnyActive());
}

TEST(AutoDriveCoordinatorTest, StopTest_TerminatesActiveTest) {
  AutoDriveCoordinator adc;
  ASSERT_TRUE(adc.StartTest(DefaultTestParams()));
  adc.StopTest();
  EXPECT_FALSE(adc.IsTestActive());
  EXPECT_FALSE(adc.IsAnyActive());
}

TEST(AutoDriveCoordinatorTest, StopSpeedCalib_TerminatesActiveProcedure) {
  AutoDriveCoordinator adc;
  ASSERT_TRUE(adc.StartSpeedCalib(0.3f, 3.0f));
  adc.StopSpeedCalib();
  EXPECT_FALSE(adc.IsSpeedCalibActive());
  EXPECT_FALSE(adc.IsAnyActive());
}

// ══════════════════════════════════════════════════════════════════════════════
// StopAll
// ══════════════════════════════════════════════════════════════════════════════

TEST(AutoDriveCoordinatorTest, StopAll_WhenNothingActive_IsNoOp) {
  AutoDriveCoordinator adc;
  adc.StopAll();
  EXPECT_FALSE(adc.IsAnyActive());
}

TEST(AutoDriveCoordinatorTest, StopAll_TerminatesTrimCalib) {
  AutoDriveCoordinator adc;
  ASSERT_TRUE(adc.StartTrimCalib(0.1f, 0.0f, 180.0f));
  adc.StopAll();
  EXPECT_FALSE(adc.IsTrimCalibActive());
  EXPECT_FALSE(adc.IsAnyActive());
}

TEST(AutoDriveCoordinatorTest, StopAll_TerminatesSpeedCalib) {
  AutoDriveCoordinator adc;
  ASSERT_TRUE(adc.StartSpeedCalib(0.3f, 3.0f));
  adc.StopAll();
  EXPECT_FALSE(adc.IsSpeedCalibActive());
  EXPECT_FALSE(adc.IsAnyActive());
}

TEST(AutoDriveCoordinatorTest, StopAll_TerminatesTestRunner) {
  AutoDriveCoordinator adc;
  ASSERT_TRUE(adc.StartTest(DefaultTestParams()));
  adc.StopAll();
  EXPECT_FALSE(adc.IsTestActive());
  EXPECT_FALSE(adc.IsAnyActive());
}

// ══════════════════════════════════════════════════════════════════════════════
// RC override suppresses auto-drive output
// ══════════════════════════════════════════════════════════════════════════════

TEST(AutoDriveCoordinatorTest, Update_RcActive_SuppressesOutputEvenIfTrimRunning) {
  AutoDriveCoordinator adc;
  ASSERT_TRUE(adc.StartTrimCalib(0.1f, 0.0f, 180.0f));
  // Procedure is active internally, but RC has control — output must be idle
  AutoDriveOutput out = adc.Update(RcActiveInput());
  EXPECT_FALSE(out.active);
  EXPECT_FLOAT_EQ(out.throttle, 0.0f);
  EXPECT_FLOAT_EQ(out.steering, 0.0f);
  // Procedure is still logically active (not stopped by RC input alone)
  EXPECT_TRUE(adc.IsTrimCalibActive());
}

TEST(AutoDriveCoordinatorTest, Update_RcActive_SuppressesSpeedCalibOutput) {
  AutoDriveCoordinator adc;
  ASSERT_TRUE(adc.StartSpeedCalib(0.3f, 3.0f));
  AutoDriveOutput out = adc.Update(RcActiveInput());
  EXPECT_FALSE(out.active);
  EXPECT_TRUE(adc.IsSpeedCalibActive());
}

TEST(AutoDriveCoordinatorTest, Update_RcActive_SuppressesTestOutput) {
  AutoDriveCoordinator adc;
  ASSERT_TRUE(adc.StartTest(DefaultTestParams()));
  AutoDriveOutput out = adc.Update(RcActiveInput());
  EXPECT_FALSE(out.active);
  EXPECT_TRUE(adc.IsTestActive());
}

// ══════════════════════════════════════════════════════════════════════════════
// After Stop, a new Start is accepted (procedure can be restarted)
// ══════════════════════════════════════════════════════════════════════════════

TEST(AutoDriveCoordinatorTest, AfterStopTrim_NewStartSucceeds) {
  AutoDriveCoordinator adc;
  ASSERT_TRUE(adc.StartTrimCalib(0.1f, 0.0f, 180.0f));
  adc.StopTrimCalib();
  ASSERT_FALSE(adc.IsAnyActive());

  bool ok = adc.StartTrimCalib(0.1f, 0.0f, 180.0f);
  EXPECT_TRUE(ok);
  EXPECT_TRUE(adc.IsTrimCalibActive());
}

TEST(AutoDriveCoordinatorTest, AfterStopAll_NewSpeedCalibSucceeds) {
  AutoDriveCoordinator adc;
  ASSERT_TRUE(adc.StartSpeedCalib(0.3f, 3.0f));
  adc.StopAll();
  ASSERT_FALSE(adc.IsAnyActive());

  bool ok = adc.StartSpeedCalib(0.3f, 3.0f);
  EXPECT_TRUE(ok);
  EXPECT_TRUE(adc.IsSpeedCalibActive());
}

TEST(AutoDriveCoordinatorTest, AfterStopAll_DifferentProcedureCanStart) {
  AutoDriveCoordinator adc;
  ASSERT_TRUE(adc.StartTrimCalib(0.1f, 0.0f, 180.0f));
  adc.StopAll();

  bool ok = adc.StartTest(DefaultTestParams());
  EXPECT_TRUE(ok);
  EXPECT_TRUE(adc.IsTestActive());
  EXPECT_FALSE(adc.IsTrimCalibActive());
}

// ══════════════════════════════════════════════════════════════════════════════
// TelemetryEventLog integration
// ══════════════════════════════════════════════════════════════════════════════

TEST(AutoDriveCoordinatorTest, EventLog_TrimCalibStart_LogsEvent) {
  AutoDriveCoordinator adc;
  TelemetryEventLog log;
  adc.SetEventLog(&log);

  size_t before = log.Count();
  ASSERT_TRUE(adc.StartTrimCalib(0.1f, 0.0f, 180.0f));
  EXPECT_GT(log.Count(), before);

  TelemetryEvent ev{};
  ASSERT_TRUE(log.GetEvent(log.Count() - 1, ev));
  EXPECT_EQ(ev.type, TelemetryEventType::TrimCalibStart);
  EXPECT_NEAR(ev.value1, 0.1f, 1e-5f);
}

TEST(AutoDriveCoordinatorTest, EventLog_SpeedCalibStart_LogsEvent) {
  AutoDriveCoordinator adc;
  TelemetryEventLog log;
  adc.SetEventLog(&log);

  ASSERT_TRUE(adc.StartSpeedCalib(0.3f, 3.0f));
  TelemetryEvent ev{};
  ASSERT_TRUE(log.GetEvent(log.Count() - 1, ev));
  EXPECT_EQ(ev.type, TelemetryEventType::SpeedCalibStart);
  EXPECT_NEAR(ev.value1, 0.3f, 1e-5f);
  EXPECT_NEAR(ev.value2, 3.0f, 1e-5f);
}

TEST(AutoDriveCoordinatorTest, EventLog_TestStart_LogsEventWithType) {
  AutoDriveCoordinator adc;
  TelemetryEventLog log;
  adc.SetEventLog(&log);

  TestParams p = DefaultTestParams();
  p.type = TestType::Circle;
  p.steering = 0.5f;
  ASSERT_TRUE(adc.StartTest(p));

  TelemetryEvent ev{};
  ASSERT_TRUE(log.GetEvent(log.Count() - 1, ev));
  EXPECT_EQ(ev.type, TelemetryEventType::TestStart);
  EXPECT_EQ(ev.param, static_cast<uint8_t>(TestType::Circle));
  EXPECT_NEAR(ev.value2, 0.5f, 1e-5f);
}

TEST(AutoDriveCoordinatorTest, EventLog_ComCalibStart_LogsEvent) {
  AutoDriveCoordinator adc;
  TelemetryEventLog log;
  adc.SetEventLog(&log);

  ASSERT_TRUE(adc.StartComCalib(0.1f, 0.5f, 5.0f, nullptr));
  TelemetryEvent ev{};
  ASSERT_TRUE(log.GetEvent(log.Count() - 1, ev));
  EXPECT_EQ(ev.type, TelemetryEventType::ComCalibStart);
  EXPECT_NEAR(ev.value1, 0.1f, 1e-5f);
  EXPECT_NEAR(ev.value2, 0.5f, 1e-5f);
}

TEST(AutoDriveCoordinatorTest, EventLog_StopAll_LogsActiveFailedEvent) {
  AutoDriveCoordinator adc;
  TelemetryEventLog log;
  adc.SetEventLog(&log);

  ASSERT_TRUE(adc.StartTrimCalib(0.1f, 0.0f, 180.0f));
  size_t before = log.Count();
  adc.StopAll();
  // StopAll must log exactly one event for the active trim procedure
  EXPECT_GT(log.Count(), before);

  TelemetryEvent ev{};
  ASSERT_TRUE(log.GetEvent(log.Count() - 1, ev));
  EXPECT_EQ(ev.type, TelemetryEventType::TrimCalibFailed);
}

TEST(AutoDriveCoordinatorTest, EventLog_StopAll_WhenNothingActive_NoExtraEvent) {
  AutoDriveCoordinator adc;
  TelemetryEventLog log;
  adc.SetEventLog(&log);

  size_t before = log.Count();
  adc.StopAll();
  // Nothing was active — no event should be appended
  EXPECT_EQ(log.Count(), before);
}

TEST(AutoDriveCoordinatorTest, EventLog_NullLog_DoesNotCrashOnStart) {
  AutoDriveCoordinator adc;
  adc.SetEventLog(nullptr);
  // Must not crash even without a log attached
  EXPECT_TRUE(adc.StartTrimCalib(0.1f, 0.0f, 180.0f));
}

TEST(AutoDriveCoordinatorTest, EventLog_NullLog_DoesNotCrashOnStopAll) {
  AutoDriveCoordinator adc;
  adc.SetEventLog(nullptr);
  ASSERT_TRUE(adc.StartSpeedCalib(0.3f, 3.0f));
  adc.StopAll();  // must not crash
  EXPECT_FALSE(adc.IsAnyActive());
}
