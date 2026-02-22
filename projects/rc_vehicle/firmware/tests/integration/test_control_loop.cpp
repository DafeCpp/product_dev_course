#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mock_platform.hpp"
#include "test_helpers.hpp"

using namespace rc_vehicle;
using namespace rc_vehicle::testing;
using ::testing::_;
using ::testing::AtLeast;
using ::testing::Return;

// ═══════════════════════════════════════════════════════════════════════════
// Basic Integration Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(ControlLoopIntegrationTest, MockPlatformBasicUsage) {
  MockPlatform mock;

  // Setup expectations
  EXPECT_CALL(mock, InitPwm()).WillOnce(Return(PlatformError::Ok));
  EXPECT_CALL(mock, InitRc()).WillOnce(Return(PlatformError::Ok));
  EXPECT_CALL(mock, InitImu()).WillOnce(Return(PlatformError::Ok));
  EXPECT_CALL(mock, InitFailsafe()).WillOnce(Return(PlatformError::Ok));

  // Execute
  EXPECT_EQ(mock.InitPwm(), PlatformError::Ok);
  EXPECT_EQ(mock.InitRc(), PlatformError::Ok);
  EXPECT_EQ(mock.InitImu(), PlatformError::Ok);
  EXPECT_EQ(mock.InitFailsafe(), PlatformError::Ok);
}

TEST(ControlLoopIntegrationTest, FakePlatformBasicUsage) {
  FakePlatform fake;

  // Test PWM output
  fake.SetPwm(0.5f, -0.3f);
  EXPECT_FLOAT_EQ(fake.GetLastThrottle(), 0.5f);
  EXPECT_FLOAT_EQ(fake.GetLastSteering(), -0.3f);
  EXPECT_EQ(fake.GetPwmSetCount(), 1);

  // Test time
  fake.SetTimeMs(1000);
  EXPECT_EQ(fake.GetTimeMs(), 1000u);

  fake.AdvanceTimeMs(500);
  EXPECT_EQ(fake.GetTimeMs(), 1500u);
}

// ═══════════════════════════════════════════════════════════════════════════
// RC Input to PWM Output Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(ControlLoopIntegrationTest, RcCommandPassthrough) {
  FakePlatform fake;

  // Simulate RC input
  RcCommand rc_cmd{.throttle = 0.75f, .steering = 0.25f};
  fake.SetRcCommand(rc_cmd);

  // Read RC input
  auto cmd = fake.GetRc();
  ASSERT_TRUE(cmd.has_value());
  EXPECT_FLOAT_EQ(cmd->throttle, 0.75f);
  EXPECT_FLOAT_EQ(cmd->steering, 0.25f);

  // Set PWM output
  fake.SetPwm(cmd->throttle, cmd->steering);
  EXPECT_FLOAT_EQ(fake.GetLastThrottle(), 0.75f);
  EXPECT_FLOAT_EQ(fake.GetLastSteering(), 0.25f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Failsafe Integration Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(ControlLoopIntegrationTest, FailsafeActivation) {
  FakePlatform fake;

  // No control sources active
  bool failsafe = fake.FailsafeUpdate(false, false);
  EXPECT_TRUE(failsafe) << "Failsafe should activate with no control";
  EXPECT_TRUE(fake.FailsafeIsActive());

  // Set neutral PWM when failsafe is active
  if (failsafe) {
    fake.SetPwmNeutral();
    EXPECT_FLOAT_EQ(fake.GetLastThrottle(), 0.0f);
    EXPECT_FLOAT_EQ(fake.GetLastSteering(), 0.0f);
  }
}

TEST(ControlLoopIntegrationTest, FailsafeRecovery) {
  FakePlatform fake;

  // Activate failsafe
  fake.FailsafeUpdate(false, false);
  EXPECT_TRUE(fake.FailsafeIsActive());

  // Recover with RC
  bool failsafe = fake.FailsafeUpdate(true, false);
  EXPECT_FALSE(failsafe) << "Failsafe should deactivate with RC active";
  EXPECT_FALSE(fake.FailsafeIsActive());
}

// ═══════════════════════════════════════════════════════════════════════════
// IMU Integration Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(ControlLoopIntegrationTest, ImuDataFlow) {
  FakePlatform fake;

  // Set IMU data (accel in g, gyro in dps)
  ImuData imu_data = MakeImuData(0.1f, -0.05f, 0.98f, 0.1f, -0.2f, 0.05f);
  fake.SetImuData(imu_data);

  // Read IMU data
  auto data = fake.ReadImu();
  ASSERT_TRUE(data.has_value());
  EXPECT_FLOAT_EQ(data->ax, 0.1f);
  EXPECT_FLOAT_EQ(data->ay, -0.05f);
  EXPECT_FLOAT_EQ(data->az, 0.98f);
  EXPECT_FLOAT_EQ(data->gx, 0.1f);
  EXPECT_FLOAT_EQ(data->gy, -0.2f);
  EXPECT_FLOAT_EQ(data->gz, 0.05f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Calibration Integration Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(ControlLoopIntegrationTest, CalibrationSaveLoad) {
  FakePlatform fake;

  // Create calibration data (accel_bias in g, gyro_bias in dps)
  ImuCalibData calib{};
  calib.accel_bias[0] = 0.1f;
  calib.accel_bias[1] = -0.05f;
  calib.accel_bias[2] = 0.2f;
  calib.valid = true;

  // Save calibration
  bool saved = fake.SaveCalib(calib);
  EXPECT_TRUE(saved);

  // Load calibration
  auto loaded = fake.LoadCalib();
  ASSERT_TRUE(loaded.has_value());
  EXPECT_FLOAT_EQ(loaded->accel_bias[0], 0.1f);
  EXPECT_FLOAT_EQ(loaded->accel_bias[1], -0.05f);
  EXPECT_FLOAT_EQ(loaded->accel_bias[2], 0.2f);
  EXPECT_TRUE(loaded->valid);
}

// ═══════════════════════════════════════════════════════════════════════════
// WebSocket Integration Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(ControlLoopIntegrationTest, TelemetrySending) {
  FakePlatform fake;

  // Send telemetry
  std::string telem_json = R"({"seq":42,"ax":1000})";
  fake.SendTelem(telem_json);

  EXPECT_EQ(fake.GetTelemSendCount(), 1);
  EXPECT_EQ(fake.GetLastTelem(), telem_json);
}

TEST(ControlLoopIntegrationTest, WiFiCommandFlow) {
  FakePlatform fake;

  // Send WiFi command
  fake.SendWifiCommand(0.6f, -0.4f);

  // Receive WiFi command
  auto cmd = fake.TryReceiveWifiCommand();
  ASSERT_TRUE(cmd.has_value());
  EXPECT_FLOAT_EQ(cmd->throttle, 0.6f);
  EXPECT_FLOAT_EQ(cmd->steering, -0.4f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Time Management Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(ControlLoopIntegrationTest, TimeProgression) {
  FakePlatform fake;

  uint32_t start_time = fake.GetTimeMs();
  EXPECT_EQ(start_time, 0u);

  fake.AdvanceTimeMs(100);
  EXPECT_EQ(fake.GetTimeMs(), 100u);

  fake.AdvanceTimeMs(50);
  EXPECT_EQ(fake.GetTimeMs(), 150u);

  // Test microseconds
  EXPECT_EQ(fake.GetTimeUs(), 150000u);
}

// ═══════════════════════════════════════════════════════════════════════════
// Mock Verification Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(ControlLoopIntegrationTest, MockCallVerification) {
  MockPlatform mock;

  // Expect specific PWM calls
  EXPECT_CALL(mock, SetPwm(0.5f, 0.0f)).Times(1);
  EXPECT_CALL(mock, SetPwm(0.0f, 0.5f)).Times(1);

  // Execute
  mock.SetPwm(0.5f, 0.0f);
  mock.SetPwm(0.0f, 0.5f);

  // Verification happens automatically in destructor
}

TEST(ControlLoopIntegrationTest, MockWithMatchers) {
  MockPlatform mock;

  // Use matchers for flexible expectations
  EXPECT_CALL(mock, SetPwm(::testing::Ge(0.0f), ::testing::Le(1.0f)))
      .Times(AtLeast(1));

  mock.SetPwm(0.5f, 0.3f);
  mock.SetPwm(0.7f, 0.1f);
}