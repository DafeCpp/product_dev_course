
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "control_components.hpp"
#include "failsafe.hpp"
#include "imu_calibration.hpp"
#include "madgwick_filter.hpp"
#include "mock_platform.hpp"
#include "slew_rate.hpp"
#include "test_helpers.hpp"

using namespace rc_vehicle;
using namespace rc_vehicle::testing;
using ::testing::_;
using ::testing::AtLeast;
using ::testing::Ge;
using ::testing::Le;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrictMock;

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

TEST(ControlLoopIntegrationTest, MockPlatformInitializationFailure) {
  MockPlatform mock;

  // Test PWM init failure
  EXPECT_CALL(mock, InitPwm()).WillOnce(Return(PlatformError::PwmInitFailed));
  EXPECT_EQ(mock.InitPwm(), PlatformError::PwmInitFailed);
}

TEST(ControlLoopIntegrationTest, MockPlatformImuInitFailure) {
  MockPlatform mock;

  EXPECT_CALL(mock, InitImu()).WillOnce(Return(PlatformError::ImuInitFailed));
  EXPECT_EQ(mock.InitImu(), PlatformError::ImuInitFailed);
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

TEST(ControlLoopIntegrationTest, RcCommandNegativeValues) {
  FakePlatform fake;

  RcCommand rc_cmd{.throttle = -0.5f, .steering = -0.8f};
  fake.SetRcCommand(rc_cmd);

  auto cmd = fake.GetRc();
  ASSERT_TRUE(cmd.has_value());
  EXPECT_FLOAT_EQ(cmd->throttle, -0.5f);
  EXPECT_FLOAT_EQ(cmd->steering, -0.8f);

  fake.SetPwm(cmd->throttle, cmd->steering);
  EXPECT_FLOAT_EQ(fake.GetLastThrottle(), -0.5f);
  EXPECT_FLOAT_EQ(fake.GetLastSteering(), -0.8f);
}

TEST(ControlLoopIntegrationTest, RcCommandMaxValues) {
  FakePlatform fake;

  RcCommand rc_cmd{.throttle = 1.0f, .steering = 1.0f};
  fake.SetRcCommand(rc_cmd);

  auto cmd = fake.GetRc();
  ASSERT_TRUE(cmd.has_value());
  EXPECT_FLOAT_EQ(cmd->throttle, 1.0f);
  EXPECT_FLOAT_EQ(cmd->steering, 1.0f);
}

TEST(ControlLoopIntegrationTest, RcCommandMinValues) {
  FakePlatform fake;

  RcCommand rc_cmd{.throttle = -1.0f, .steering = -1.0f};
  fake.SetRcCommand(rc_cmd);

  auto cmd = fake.GetRc();
  ASSERT_TRUE(cmd.has_value());
  EXPECT_FLOAT_EQ(cmd->throttle, -1.0f);
  EXPECT_FLOAT_EQ(cmd->steering, -1.0f);
}

TEST(ControlLoopIntegrationTest, RcCommandZeroValues) {
  FakePlatform fake;

  RcCommand rc_cmd{.throttle = 0.0f, .steering = 0.0f};
  fake.SetRcCommand(rc_cmd);

  auto cmd = fake.GetRc();
  ASSERT_TRUE(cmd.has_value());
  EXPECT_FLOAT_EQ(cmd->throttle, 0.0f);
  EXPECT_FLOAT_EQ(cmd->steering, 0.0f);
}

TEST(ControlLoopIntegrationTest, RcCommandNotAvailable) {
  FakePlatform fake;

  // No RC command set
  auto cmd = fake.GetRc();
  EXPECT_FALSE(cmd.has_value());
}

TEST(ControlLoopIntegrationTest, RcCommandClear) {
  FakePlatform fake;

  RcCommand rc_cmd{.throttle = 0.5f, .steering = 0.3f};
  fake.SetRcCommand(rc_cmd);
  EXPECT_TRUE(fake.GetRc().has_value());

  fake.ClearRcCommand();
  EXPECT_FALSE(fake.GetRc().has_value());
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

TEST(ControlLoopIntegrationTest, FailsafeRecoveryWithWifi) {
  FakePlatform fake;

  // Activate failsafe
  fake.FailsafeUpdate(false, false);
  EXPECT_TRUE(fake.FailsafeIsActive());

  // Recover with WiFi
  bool failsafe = fake.FailsafeUpdate(false, true);
  EXPECT_FALSE(failsafe) << "Failsafe should deactivate with WiFi active";
  EXPECT_FALSE(fake.FailsafeIsActive());
}

TEST(ControlLoopIntegrationTest, FailsafeRecoveryWithBothSources) {
  FakePlatform fake;

  // Activate failsafe
  fake.FailsafeUpdate(false, false);
  EXPECT_TRUE(fake.FailsafeIsActive());

  // Recover with both sources
  bool failsafe = fake.FailsafeUpdate(true, true);
  EXPECT_FALSE(failsafe) << "Failsafe should deactivate with both sources";
  EXPECT_FALSE(fake.FailsafeIsActive());
}

TEST(ControlLoopIntegrationTest, FailsafeStaysInactiveWithRc) {
  FakePlatform fake;

  // RC active from start
  bool failsafe = fake.FailsafeUpdate(true, false);
  EXPECT_FALSE(failsafe);
  EXPECT_FALSE(fake.FailsafeIsActive());

  // Continue with RC active
  failsafe = fake.FailsafeUpdate(true, false);
  EXPECT_FALSE(failsafe);
  EXPECT_FALSE(fake.FailsafeIsActive());
}

TEST(ControlLoopIntegrationTest, FailsafeStaysInactiveWithWifi) {
  FakePlatform fake;

  // WiFi active from start
  bool failsafe = fake.FailsafeUpdate(false, true);
  EXPECT_FALSE(failsafe);
  EXPECT_FALSE(fake.FailsafeIsActive());
}

TEST(ControlLoopIntegrationTest, FailsafeWithMock) {
  MockPlatform mock;

  EXPECT_CALL(mock, FailsafeUpdate(false, false)).WillOnce(Return(true));
  EXPECT_CALL(mock, FailsafeIsActive()).WillOnce(Return(true));
  EXPECT_CALL(mock, SetPwmNeutral()).Times(1);

  bool failsafe = mock.FailsafeUpdate(false, false);
  EXPECT_TRUE(failsafe);
  EXPECT_TRUE(mock.FailsafeIsActive());
  mock.SetPwmNeutral();
}

// ═══════════════════════════════════════════════════════════════════════════
// Failsafe Class Integration Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(ControlLoopIntegrationTest, FailsafeClassWithFakePlatform) {
  FakePlatform fake;
  Failsafe failsafe(100);  // 100ms timeout

  // Initial state
  EXPECT_EQ(failsafe.GetState(), FailsafeState::Inactive);
  EXPECT_FALSE(failsafe.IsActive());

  // Simulate control loop with RC active
  fake.SetTimeMs(0);
  auto state = failsafe.Update(fake.GetTimeMs(), true, false);
  EXPECT_EQ(state, FailsafeState::Inactive);

  // Lose RC, but within timeout
  fake.AdvanceTimeMs(50);
  state = failsafe.Update(fake.GetTimeMs(), false, false);
  EXPECT_EQ(state, FailsafeState::Inactive);

  // Exceed timeout
  fake.AdvanceTimeMs(60);
  state = failsafe.Update(fake.GetTimeMs(), false, false);
  EXPECT_EQ(state, FailsafeState::Active);
  EXPECT_TRUE(failsafe.IsActive());

  // Apply neutral PWM
  fake.SetPwmNeutral();
  EXPECT_FLOAT_EQ(fake.GetLastThrottle(), 0.0f);
  EXPECT_FLOAT_EQ(fake.GetLastSteering(), 0.0f);
}

TEST(ControlLoopIntegrationTest, FailsafeClassRecoverySequence) {
  FakePlatform fake;
  Failsafe failsafe(100);

  // Activate failsafe
  fake.SetTimeMs(0);
  (void)failsafe.Update(fake.GetTimeMs(), false, false);
  fake.AdvanceTimeMs(150);
  auto state = failsafe.Update(fake.GetTimeMs(), false, false);
  EXPECT_EQ(state, FailsafeState::Active);

  // Start recovery with RC
  fake.AdvanceTimeMs(10);
  state = failsafe.Update(fake.GetTimeMs(), true, false);
  EXPECT_EQ(state, FailsafeState::Recovering);

  // Complete recovery
  fake.AdvanceTimeMs(10);
  state = failsafe.Update(fake.GetTimeMs(), true, false);
  EXPECT_EQ(state, FailsafeState::Inactive);
  EXPECT_FALSE(failsafe.IsActive());
}

// ═══════════════════════════════════════════════════════════════════════════
// Failsafe End-to-End (RC/WiFi loss scenarios)
// Simulates one control loop tick: source selection → Failsafe (with timeout)
// → PWM or neutral.
// ═══════════════════════════════════════════════════════════════════════════

namespace {

void RunControlLoopTick(FakePlatform& fake, Failsafe& failsafe) {
  const uint32_t now = fake.GetTimeMs();
  const bool rc_active = fake.GetRc().has_value();
  const bool wifi_active = fake.TryReceiveWifiCommand().has_value();
  float throttle = 0.0f;
  float steering = 0.0f;
  if (rc_active) {
    auto cmd = fake.GetRc();
    if (cmd) {
      throttle = cmd->throttle;
      steering = cmd->steering;
    }
  } else if (wifi_active) {
    auto cmd = fake.TryReceiveWifiCommand();
    if (cmd) {
      throttle = cmd->throttle;
      steering = cmd->steering;
    }
  }
  (void)failsafe.Update(now, rc_active, wifi_active);
  if (failsafe.IsActive()) {
    fake.SetPwmNeutral();
  } else {
    fake.SetPwm(throttle, steering);
  }
}

}  // namespace

TEST(ControlLoopIntegrationTest, E2E_RcLoss_ActivatesFailsafeAndNeutralPwm) {
  FakePlatform fake;
  Failsafe failsafe(100);
  fake.SetTimeMs(0);

  // Drive with RC
  fake.SetRcCommand(RcCommand{0.6f, -0.2f});
  RunControlLoopTick(fake, failsafe);
  RunControlLoopTick(fake, failsafe);
  EXPECT_FALSE(failsafe.IsActive());
  EXPECT_FLOAT_EQ(fake.GetLastThrottle(), 0.6f);
  EXPECT_FLOAT_EQ(fake.GetLastSteering(), -0.2f);

  // Lose RC
  fake.ClearRcCommand();
  fake.AdvanceTimeMs(50);
  RunControlLoopTick(fake, failsafe);
  EXPECT_FALSE(failsafe.IsActive()) << "Within timeout, failsafe not yet active";

  fake.AdvanceTimeMs(60);
  RunControlLoopTick(fake, failsafe);
  EXPECT_TRUE(failsafe.IsActive()) << "After timeout, failsafe should activate";
  EXPECT_FLOAT_EQ(fake.GetLastThrottle(), 0.0f);
  EXPECT_FLOAT_EQ(fake.GetLastSteering(), 0.0f);
}

TEST(ControlLoopIntegrationTest, E2E_RcRecovery_ResumesPwm) {
  FakePlatform fake;
  Failsafe failsafe(100);
  fake.SetTimeMs(0);

  fake.ClearRcCommand();
  fake.AdvanceTimeMs(150);
  RunControlLoopTick(fake, failsafe);
  EXPECT_TRUE(failsafe.IsActive());
  EXPECT_FLOAT_EQ(fake.GetLastThrottle(), 0.0f);
  EXPECT_FLOAT_EQ(fake.GetLastSteering(), 0.0f);

  // RC comes back
  fake.SetRcCommand(RcCommand{0.3f, 0.5f});
  fake.AdvanceTimeMs(10);
  RunControlLoopTick(fake, failsafe);
  EXPECT_EQ(failsafe.GetState(), FailsafeState::Recovering);
  fake.AdvanceTimeMs(15);
  RunControlLoopTick(fake, failsafe);
  EXPECT_FALSE(failsafe.IsActive());
  EXPECT_FLOAT_EQ(fake.GetLastThrottle(), 0.3f);
  EXPECT_FLOAT_EQ(fake.GetLastSteering(), 0.5f);
}

TEST(ControlLoopIntegrationTest, E2E_WiFiLoss_ActivatesFailsafeAndNeutralPwm) {
  FakePlatform fake;
  Failsafe failsafe(100);
  fake.SetTimeMs(0);

  fake.SetWifiCommand(RcCommand{-0.4f, 0.8f});
  RunControlLoopTick(fake, failsafe);
  EXPECT_FALSE(failsafe.IsActive());
  EXPECT_FLOAT_EQ(fake.GetLastThrottle(), -0.4f);
  EXPECT_FLOAT_EQ(fake.GetLastSteering(), 0.8f);

  fake.ClearWifiCommand();
  fake.AdvanceTimeMs(50);
  RunControlLoopTick(fake, failsafe);
  EXPECT_FALSE(failsafe.IsActive());

  fake.AdvanceTimeMs(60);
  RunControlLoopTick(fake, failsafe);
  EXPECT_TRUE(failsafe.IsActive());
  EXPECT_FLOAT_EQ(fake.GetLastThrottle(), 0.0f);
  EXPECT_FLOAT_EQ(fake.GetLastSteering(), 0.0f);
}

TEST(ControlLoopIntegrationTest, E2E_WiFiRecovery_ResumesPwm) {
  FakePlatform fake;
  Failsafe failsafe(100);
  fake.SetTimeMs(0);

  fake.ClearRcCommand();
  fake.ClearWifiCommand();
  fake.AdvanceTimeMs(150);
  RunControlLoopTick(fake, failsafe);
  EXPECT_TRUE(failsafe.IsActive());

  fake.SetWifiCommand(RcCommand{0.2f, -0.6f});
  fake.AdvanceTimeMs(10);
  RunControlLoopTick(fake, failsafe);
  fake.AdvanceTimeMs(15);
  RunControlLoopTick(fake, failsafe);
  EXPECT_FALSE(failsafe.IsActive());
  EXPECT_FLOAT_EQ(fake.GetLastThrottle(), 0.2f);
  EXPECT_FLOAT_EQ(fake.GetLastSteering(), -0.6f);
}

TEST(ControlLoopIntegrationTest,
     E2E_RcLossWhileWifiPresent_NoFailsafePwmFromWifi) {
  FakePlatform fake;
  Failsafe failsafe(100);
  fake.SetTimeMs(0);

  fake.SetRcCommand(RcCommand{0.5f, 0.0f});
  fake.SetWifiCommand(RcCommand{0.1f, 0.2f});
  RunControlLoopTick(fake, failsafe);
  EXPECT_FLOAT_EQ(fake.GetLastThrottle(), 0.5f) << "RC has priority";

  // Lose RC only; WiFi still present
  fake.ClearRcCommand();
  fake.AdvanceTimeMs(200);
  RunControlLoopTick(fake, failsafe);
  EXPECT_FALSE(failsafe.IsActive()) << "WiFi keeps control";
  EXPECT_FLOAT_EQ(fake.GetLastThrottle(), 0.1f);
  EXPECT_FLOAT_EQ(fake.GetLastSteering(), 0.2f);
}

TEST(ControlLoopIntegrationTest, E2E_BothSourcesLost_ActivatesFailsafe) {
  FakePlatform fake;
  Failsafe failsafe(100);
  fake.SetTimeMs(0);

  fake.SetRcCommand(RcCommand{0.7f, -0.3f});
  RunControlLoopTick(fake, failsafe);
  EXPECT_FALSE(failsafe.IsActive());

  fake.ClearRcCommand();
  fake.ClearWifiCommand();
  fake.AdvanceTimeMs(50);
  RunControlLoopTick(fake, failsafe);
  EXPECT_FALSE(failsafe.IsActive());
  fake.AdvanceTimeMs(60);
  RunControlLoopTick(fake, failsafe);
  EXPECT_TRUE(failsafe.IsActive());
  EXPECT_FLOAT_EQ(fake.GetLastThrottle(), 0.0f);
  EXPECT_FLOAT_EQ(fake.GetLastSteering(), 0.0f);
}

TEST(ControlLoopIntegrationTest, E2E_AlternatingLoss_RcThenWifiRecovery) {
  FakePlatform fake;
  Failsafe failsafe(100);
  fake.SetTimeMs(0);

  // Start with RC, lose it -> failsafe
  fake.SetRcCommand(RcCommand{0.5f, 0.0f});
  RunControlLoopTick(fake, failsafe);
  fake.ClearRcCommand();
  fake.AdvanceTimeMs(150);
  RunControlLoopTick(fake, failsafe);
  EXPECT_TRUE(failsafe.IsActive());

  // Recover with WiFi (no RC)
  fake.SetWifiCommand(RcCommand{-0.3f, 0.4f});
  fake.AdvanceTimeMs(20);
  RunControlLoopTick(fake, failsafe);
  EXPECT_FALSE(failsafe.IsActive());
  EXPECT_FLOAT_EQ(fake.GetLastThrottle(), -0.3f);
  EXPECT_FLOAT_EQ(fake.GetLastSteering(), 0.4f);

  // Lose WiFi -> failsafe again
  fake.ClearWifiCommand();
  fake.AdvanceTimeMs(150);
  RunControlLoopTick(fake, failsafe);
  EXPECT_TRUE(failsafe.IsActive());
  EXPECT_FLOAT_EQ(fake.GetLastThrottle(), 0.0f);
  EXPECT_FLOAT_EQ(fake.GetLastSteering(), 0.0f);

  // Recover with RC
  fake.SetRcCommand(RcCommand{0.0f, -0.5f});
  fake.AdvanceTimeMs(20);
  RunControlLoopTick(fake, failsafe);
  EXPECT_FALSE(failsafe.IsActive());
  EXPECT_FLOAT_EQ(fake.GetLastThrottle(), 0.0f);
  EXPECT_FLOAT_EQ(fake.GetLastSteering(), -0.5f);
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

TEST(ControlLoopIntegrationTest, ImuDataNotAvailable) {
  FakePlatform fake;

  // No IMU data set
  auto data = fake.ReadImu();
  EXPECT_FALSE(data.has_value());
}

TEST(ControlLoopIntegrationTest, ImuDataGravityOnly) {
  FakePlatform fake;

  // Stationary device with gravity pointing down
  ImuData imu_data = MakeImuData(0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f);
  fake.SetImuData(imu_data);

  auto data = fake.ReadImu();
  ASSERT_TRUE(data.has_value());
  EXPECT_FLOAT_EQ(data->az, 1.0f);
  EXPECT_FLOAT_EQ(data->gx, 0.0f);
  EXPECT_FLOAT_EQ(data->gy, 0.0f);
  EXPECT_FLOAT_EQ(data->gz, 0.0f);
}

TEST(ControlLoopIntegrationTest, ImuDataWithRotation) {
  FakePlatform fake;

  // Device rotating around Z axis
  ImuData imu_data = MakeImuData(0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 90.0f);
  fake.SetImuData(imu_data);

  auto data = fake.ReadImu();
  ASSERT_TRUE(data.has_value());
  EXPECT_FLOAT_EQ(data->gz, 90.0f);  // 90 dps rotation
}

TEST(ControlLoopIntegrationTest, ImuDataWithMock) {
  MockPlatform mock;

  ImuData expected_data = MakeImuData(0.1f, 0.2f, 0.98f, 1.0f, 2.0f, 3.0f);
  EXPECT_CALL(mock, ReadImu()).WillOnce(Return(expected_data));

  auto data = mock.ReadImu();
  ASSERT_TRUE(data.has_value());
  EXPECT_FLOAT_EQ(data->ax, 0.1f);
  EXPECT_FLOAT_EQ(data->gx, 1.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// IMU with Madgwick Filter Integration Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(ControlLoopIntegrationTest, ImuWithMadgwickFilter) {
  FakePlatform fake;
  MadgwickFilter filter;

  // Set IMU data (stationary with gravity)
  ImuData imu_data = MakeImuData(0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f);
  fake.SetImuData(imu_data);

  // Read and process IMU data
  auto data = fake.ReadImu();
  ASSERT_TRUE(data.has_value());

  // Update filter
  filter.Update(data->ax, data->ay, data->az, data->gx, data->gy, data->gz,
                0.01f);

  // Check quaternion is normalized
  float qw, qx, qy, qz;
  filter.GetQuaternion(qw, qx, qy, qz);
  EXPECT_TRUE(IsQuaternionNormalized(qw, qx, qy, qz));
}

TEST(ControlLoopIntegrationTest, ImuWithMadgwickFilterMultipleUpdates) {
  FakePlatform fake;
  MadgwickFilter filter;

  // Simulate multiple IMU readings
  for (int i = 0; i < 100; ++i) {
    ImuData imu_data = MakeImuData(0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f);
    fake.SetImuData(imu_data);

    auto data = fake.ReadImu();
    ASSERT_TRUE(data.has_value());

    filter.Update(data->ax, data->ay, data->az, data->gx, data->gy, data->gz,
                  0.002f);  // 500 Hz
  }

  // Check quaternion is still normalized
  float qw, qx, qy, qz;
  filter.GetQuaternion(qw, qx, qy, qz);
  EXPECT_TRUE(IsQuaternionNormalized(qw, qx, qy, qz));

  // Check Euler angles are reasonable (pitch, roll, yaw in degrees)
  float pitch_deg, roll_deg, yaw_deg;
  filter.GetEulerDeg(pitch_deg, roll_deg, yaw_deg);
  EXPECT_NEAR(roll_deg, 0.0f, 5.0f);
  EXPECT_NEAR(pitch_deg, 0.0f, 5.0f);
}

TEST(ControlLoopIntegrationTest, ImuWithMadgwickFilterRotation) {
  FakePlatform fake;
  MadgwickFilter filter;

  // Simulate rotation around Z axis
  for (int i = 0; i < 50; ++i) {
    // Constant rotation rate of 90 dps around Z
    ImuData imu_data = MakeImuData(0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 90.0f);
    fake.SetImuData(imu_data);

    auto data = fake.ReadImu();
    ASSERT_TRUE(data.has_value());

    filter.Update(data->ax, data->ay, data->az, data->gx, data->gy, data->gz,
                  0.01f);  // 100 Hz
  }

  // Check quaternion is normalized
  float qw, qx, qy, qz;
  filter.GetQuaternion(qw, qx, qy, qz);
  EXPECT_TRUE(IsQuaternionNormalized(qw, qx, qy, qz));
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

TEST(ControlLoopIntegrationTest, CalibrationNotAvailable) {
  FakePlatform fake;

  // No calibration saved
  auto loaded = fake.LoadCalib();
  EXPECT_FALSE(loaded.has_value());
}

TEST(ControlLoopIntegrationTest, CalibrationWithGyroBias) {
  FakePlatform fake;

  ImuCalibData calib{};
  calib.gyro_bias[0] = 1.5f;
  calib.gyro_bias[1] = -0.8f;
  calib.gyro_bias[2] = 0.3f;
  calib.valid = true;

  fake.SaveCalib(calib);
  auto loaded = fake.LoadCalib();

  ASSERT_TRUE(loaded.has_value());
  EXPECT_FLOAT_EQ(loaded->gyro_bias[0], 1.5f);
  EXPECT_FLOAT_EQ(loaded->gyro_bias[1], -0.8f);
  EXPECT_FLOAT_EQ(loaded->gyro_bias[2], 0.3f);
}

TEST(ControlLoopIntegrationTest, CalibrationWithMock) {
  MockPlatform mock;

  ImuCalibData calib{};
  calib.accel_bias[0] = 0.1f;
  calib.valid = true;

  EXPECT_CALL(mock, SaveCalib(_)).WillOnce(Return(true));
  EXPECT_CALL(mock, LoadCalib()).WillOnce(Return(calib));

  EXPECT_TRUE(mock.SaveCalib(calib));
  auto loaded = mock.LoadCalib();
  ASSERT_TRUE(loaded.has_value());
  EXPECT_FLOAT_EQ(loaded->accel_bias[0], 0.1f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Calibration flow End-to-End (IMU calibration scenarios)
// Full flow: StartCalibration → FeedSample in loop → Done → Save/Load → Apply
// ═══════════════════════════════════════════════════════════════════════════

TEST(ControlLoopIntegrationTest, E2E_Calibration_GyroOnly_CollectDone_SaveLoad) {
  FakePlatform fake;
  ImuCalibration calib;

  EXPECT_EQ(calib.GetStatus(), CalibStatus::Idle);
  calib.StartCalibration(CalibMode::GyroOnly, 50);
  EXPECT_EQ(calib.GetStatus(), CalibStatus::Collecting);
  EXPECT_EQ(calib.GetCalibStage(), 1);

  // Steady samples: constant small gyro (bias), gravity Z (low variance)
  ImuData steady = MakeImuData(0.0f, 0.0f, 1.0f, 0.15f, -0.08f, 0.03f);
  for (int i = 0; i < 50; ++i) {
    calib.FeedSample(steady);
  }
  EXPECT_EQ(calib.GetStatus(), CalibStatus::Done);
  EXPECT_TRUE(calib.IsValid());
  EXPECT_NEAR(calib.GetData().gyro_bias[0], 0.15f, 0.01f);
  EXPECT_NEAR(calib.GetData().gyro_bias[1], -0.08f, 0.01f);
  EXPECT_NEAR(calib.GetData().gyro_bias[2], 0.03f, 0.01f);

  // Save to platform, load into new instance
  bool saved = fake.SaveCalib(calib.GetData());
  EXPECT_TRUE(saved);
  ImuCalibration calib2;
  auto loaded = fake.LoadCalib();
  ASSERT_TRUE(loaded.has_value());
  calib2.SetData(*loaded);
  EXPECT_TRUE(calib2.IsValid());

  // Apply: raw minus bias
  ImuData raw = MakeImuData(0.0f, 0.0f, 1.0f, 0.5f, -0.3f, 0.2f);
  calib2.Apply(raw);
  EXPECT_NEAR(raw.gx, 0.5f - loaded->gyro_bias[0], 1e-5f);
  EXPECT_NEAR(raw.gy, -0.3f - loaded->gyro_bias[1], 1e-5f);
  EXPECT_NEAR(raw.gz, 0.2f - loaded->gyro_bias[2], 1e-5f);
}

TEST(ControlLoopIntegrationTest, E2E_Calibration_Full_CollectDone_GravityVec_SaveLoad) {
  FakePlatform fake;
  ImuCalibration calib;

  calib.StartCalibration(CalibMode::Full, 100);
  ImuData steady = MakeImuData(0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f);
  for (int i = 0; i < 100; ++i) {
    calib.FeedSample(steady);
  }
  EXPECT_EQ(calib.GetStatus(), CalibStatus::Done);
  EXPECT_TRUE(calib.IsValid());
  EXPECT_NEAR(calib.GetData().gravity_vec[2], 1.0f, 0.01f);

  bool saved = fake.SaveCalib(calib.GetData());
  EXPECT_TRUE(saved);
  auto loaded = fake.LoadCalib();
  ASSERT_TRUE(loaded.has_value());
  ImuCalibration calib2;
  calib2.SetData(*loaded);
  EXPECT_TRUE(calib2.IsValid());

  // Apply: raw (with known bias) -> result should have bias subtracted
  ImuData raw = MakeImuData(0.02f, -0.01f, 1.02f, 0.1f, -0.05f, 0.02f);
  const float ax_before = raw.ax, gx_before = raw.gx;
  calib2.Apply(raw);
  EXPECT_NEAR(raw.ax, ax_before - loaded->accel_bias[0], 1e-5f);
  EXPECT_NEAR(raw.gx, gx_before - loaded->gyro_bias[0], 1e-5f);
}

TEST(ControlLoopIntegrationTest, E2E_Calibration_FullThenForward_ForwardVecSet) {
  ImuCalibration calib;

  // Stage 1: Full (standing still)
  calib.StartCalibration(CalibMode::Full, 100);
  ImuData steady = MakeImuData(0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f);
  for (int i = 0; i < 100; ++i) {
    calib.FeedSample(steady);
  }
  ASSERT_EQ(calib.GetStatus(), CalibStatus::Done);
  ASSERT_TRUE(calib.IsValid());

  // Stage 2: Forward (linear accel above threshold)
  bool started = calib.StartForwardCalibration(150);
  EXPECT_TRUE(started);
  EXPECT_EQ(calib.GetCalibStage(), 2);
  // Linear accel = calibrated accel - gravity. Use gravity (0,0,1), add
  // forward (0.1,0,0) -> raw accel (0.1, 0, 1) with bias 0,0,0
  ImuData with_forward = MakeImuData(0.1f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f);
  for (int i = 0; i < 150; ++i) {
    calib.FeedSample(with_forward);
  }
  EXPECT_EQ(calib.GetStatus(), CalibStatus::Done);
  EXPECT_TRUE(calib.IsValid());
  // Forward direction should be +X (unit vector of linear 0.1,0,0)
  EXPECT_NEAR(calib.GetData().accel_forward_vec[0], 1.0f, 0.01f);
  EXPECT_NEAR(calib.GetData().accel_forward_vec[1], 0.0f, 0.01f);
  EXPECT_NEAR(calib.GetData().accel_forward_vec[2], 0.0f, 0.01f);
}

TEST(ControlLoopIntegrationTest, E2E_Calibration_LoadFromPlatform_ThenApply) {
  FakePlatform fake;
  ImuCalibData stored{};
  stored.gyro_bias[0] = 1.0f;
  stored.gyro_bias[1] = -0.5f;
  stored.gyro_bias[2] = 0.2f;
  stored.accel_bias[0] = 0.01f;
  stored.accel_bias[1] = -0.02f;
  stored.accel_bias[2] = 0.03f;
  stored.gravity_vec[0] = 0.0f;
  stored.gravity_vec[1] = 0.0f;
  stored.gravity_vec[2] = 1.0f;
  stored.accel_forward_vec[0] = 1.0f;
  stored.accel_forward_vec[1] = 0.0f;
  stored.accel_forward_vec[2] = 0.0f;
  stored.valid = true;

  fake.SaveCalib(stored);
  auto loaded = fake.LoadCalib();
  ASSERT_TRUE(loaded.has_value());

  ImuCalibration calib;
  calib.SetData(*loaded);
  EXPECT_TRUE(calib.IsValid());

  ImuData raw = MakeImuData(0.5f, -0.3f, 1.1f, 2.0f, -1.0f, 0.5f);
  calib.Apply(raw);
  EXPECT_FLOAT_EQ(raw.gx, 2.0f - 1.0f);
  EXPECT_FLOAT_EQ(raw.gy, -1.0f - (-0.5f));
  EXPECT_FLOAT_EQ(raw.gz, 0.5f - 0.2f);
  EXPECT_FLOAT_EQ(raw.ax, 0.5f - 0.01f);
  EXPECT_FLOAT_EQ(raw.ay, -0.3f - (-0.02f));
  EXPECT_FLOAT_EQ(raw.az, 1.1f - 0.03f);
}

TEST(ControlLoopIntegrationTest, E2E_Calibration_MotionDetected_Fails) {
  ImuCalibration calib;

  calib.StartCalibration(CalibMode::GyroOnly, 30);
  // High variance gyro (motion) -> Finalize fails
  for (int i = 0; i < 30; ++i) {
    float t = static_cast<float>(i) * 0.5f;
    ImuData moving = MakeImuData(0.0f, 0.0f, 1.0f, t, -t, 0.1f);
    calib.FeedSample(moving);
  }
  EXPECT_EQ(calib.GetStatus(), CalibStatus::Failed);
  EXPECT_FALSE(calib.IsValid());
}

// ═══════════════════════════════════════════════════════════════════════════
// Stabilization Config Integration Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(ControlLoopIntegrationTest, StabilizationConfigSaveLoad) {
  FakePlatform fake;

  StabilizationConfig config{};
  config.enabled = true;
  config.madgwick_beta = 0.2f;
  config.lpf_cutoff_hz = 25.0f;
  config.imu_sample_rate_hz = 500.0f;

  bool saved = fake.SaveStabilizationConfig(config);
  EXPECT_TRUE(saved);

  auto loaded = fake.LoadStabilizationConfig();
  ASSERT_TRUE(loaded.has_value());
  EXPECT_TRUE(loaded->enabled);
  EXPECT_FLOAT_EQ(loaded->madgwick_beta, 0.2f);
  EXPECT_FLOAT_EQ(loaded->lpf_cutoff_hz, 25.0f);
  EXPECT_FLOAT_EQ(loaded->imu_sample_rate_hz, 500.0f);
}

TEST(ControlLoopIntegrationTest, StabilizationConfigNotAvailable) {
  FakePlatform fake;

  auto loaded = fake.LoadStabilizationConfig();
  EXPECT_FALSE(loaded.has_value());
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

TEST(ControlLoopIntegrationTest, TelemetrySendingMultiple) {
  FakePlatform fake;

  fake.SendTelem(R"({"seq":1})");
  fake.SendTelem(R"({"seq":2})");
  fake.SendTelem(R"({"seq":3})");

  EXPECT_EQ(fake.GetTelemSendCount(), 3);
  EXPECT_EQ(fake.GetLastTelem(), R"({"seq":3})");
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

TEST(ControlLoopIntegrationTest, WiFiCommandNotAvailable) {
  FakePlatform fake;

  auto cmd = fake.TryReceiveWifiCommand();
  EXPECT_FALSE(cmd.has_value());
}

TEST(ControlLoopIntegrationTest, WiFiCommandClear) {
  FakePlatform fake;

  fake.SendWifiCommand(0.5f, 0.3f);
  EXPECT_TRUE(fake.TryReceiveWifiCommand().has_value());

  fake.ClearWifiCommand();
  EXPECT_FALSE(fake.TryReceiveWifiCommand().has_value());
}

TEST(ControlLoopIntegrationTest, WebSocketClientCount) {
  FakePlatform fake;

  EXPECT_EQ(fake.GetWebSocketClientCount(), 0u);

  fake.SetWebSocketClientCount(3);
  EXPECT_EQ(fake.GetWebSocketClientCount(), 3u);
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

TEST(ControlLoopIntegrationTest, TimeSet) {
  FakePlatform fake;

  fake.SetTimeMs(5000);
  EXPECT_EQ(fake.GetTimeMs(), 5000u);
  EXPECT_EQ(fake.GetTimeUs(), 5000000u);
}

TEST(ControlLoopIntegrationTest, DelayUntilNextTick) {
  FakePlatform fake;

  fake.SetTimeMs(0);
  fake.DelayUntilNextTick(10);
  EXPECT_EQ(fake.GetTimeMs(), 10u);

  fake.DelayUntilNextTick(10);
  EXPECT_EQ(fake.GetTimeMs(), 20u);
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
  EXPECT_CALL(mock, SetPwm(Ge(0.0f), Le(1.0f))).Times(AtLeast(1));

  mock.SetPwm(0.5f, 0.3f);
  mock.SetPwm(0.7f, 0.1f);

  // Verification happens automatically in destructor
}

// ═══════════════════════════════════════════════════════════════════════════
// Yaw-Rate PID Stabilization Integration Tests
//
// Tests the full yaw stabilization loop:
//   steering command → desired yaw rate → PID(error) → steering correction
// ═══════════════════════════════════════════════════════════════════════════

#include "pid_controller.hpp"
#include "stabilization_config.hpp"

using rc_vehicle::PidController;

namespace {

// Simulate one stabilization tick: given commanded_steering and actual_gz,
// return the corrected steering value produced by the PID controller.
float RunStabTick(PidController& pid, float commanded_steering,
                  float actual_gz_dps, float steer_to_yaw_rate_dps,
                  float stab_weight, float dt_sec) {
  const float omega_desired = steer_to_yaw_rate_dps * commanded_steering;
  const float pid_out = pid.Step(omega_desired - actual_gz_dps, dt_sec);
  float corrected = commanded_steering + pid_out * stab_weight;
  if (corrected > 1.0f) corrected = 1.0f;
  if (corrected < -1.0f) corrected = -1.0f;
  return corrected;
}

}  // namespace

TEST(ControlLoopIntegrationTest, YawPid_ZeroError_NoCorrectionApplied) {
  // When actual yaw rate matches desired exactly → PID output = 0
  PidController pid({.kp = 0.1f, .ki = 0.0f, .kd = 0.0f,
                     .max_integral = 1.0f, .max_output = 0.3f});
  const float commanded = 0.5f;
  const float steer_to_dps = 90.0f;
  const float actual_gz = steer_to_dps * commanded;  // exact match

  float corrected = RunStabTick(pid, commanded, actual_gz, steer_to_dps, 1.0f, 0.002f);
  EXPECT_FLOAT_EQ(corrected, commanded) << "No error → no correction";
}

TEST(ControlLoopIntegrationTest, YawPid_ActualTooFast_SteeringReduced) {
  // Vehicle turns too fast (overshoot) → PID corrects by reducing steering
  PidController pid({.kp = 0.1f, .ki = 0.0f, .kd = 0.0f,
                     .max_integral = 1.0f, .max_output = 1.0f});
  const float commanded = 0.5f;
  const float steer_to_dps = 90.0f;
  const float desired_gz = steer_to_dps * commanded;  // 45 dps
  const float actual_gz = desired_gz + 20.0f;         // 65 dps (too fast)

  float corrected = RunStabTick(pid, commanded, actual_gz, steer_to_dps, 1.0f, 0.002f);
  EXPECT_LT(corrected, commanded) << "Too fast → steering reduced";
}

TEST(ControlLoopIntegrationTest, YawPid_ActualTooSlow_SteeringIncreased) {
  // Vehicle turns too slow (undershoot) → PID corrects by increasing steering
  PidController pid({.kp = 0.1f, .ki = 0.0f, .kd = 0.0f,
                     .max_integral = 1.0f, .max_output = 1.0f});
  const float commanded = 0.5f;
  const float steer_to_dps = 90.0f;
  const float desired_gz = steer_to_dps * commanded;  // 45 dps
  const float actual_gz = desired_gz - 20.0f;         // 25 dps (too slow)

  float corrected = RunStabTick(pid, commanded, actual_gz, steer_to_dps, 1.0f, 0.002f);
  EXPECT_GT(corrected, commanded) << "Too slow → steering increased";
}

TEST(ControlLoopIntegrationTest, YawPid_ZeroWeight_NoEffect) {
  // stab_weight = 0 → stabilization disabled, commanded steering unchanged
  PidController pid({.kp = 10.0f, .ki = 0.0f, .kd = 0.0f,
                     .max_integral = 1.0f, .max_output = 1.0f});
  const float commanded = 0.6f;
  float corrected =
      RunStabTick(pid, commanded, 0.0f, 90.0f, /*weight=*/0.0f, 0.002f);
  EXPECT_FLOAT_EQ(corrected, commanded) << "Zero weight → no effect";
}

TEST(ControlLoopIntegrationTest, YawPid_CorrectionClampedAtPlusOne) {
  // Very large correction must not exceed +1.0
  PidController pid({.kp = 10.0f, .ki = 0.0f, .kd = 0.0f,
                     .max_integral = 1.0f, .max_output = 10.0f});
  // commanded = 0.9, actual_gz = 0 (desired = 81 dps error = 81 dps)
  float corrected = RunStabTick(pid, 0.9f, 0.0f, 90.0f, 1.0f, 0.002f);
  EXPECT_LE(corrected, 1.0f);
}

TEST(ControlLoopIntegrationTest, YawPid_CorrectionClampedAtMinusOne) {
  // Large negative error must not go below -1.0
  PidController pid({.kp = 10.0f, .ki = 0.0f, .kd = 0.0f,
                     .max_integral = 1.0f, .max_output = 10.0f});
  float corrected = RunStabTick(pid, -0.9f, 0.0f, 90.0f, 1.0f, 0.002f);
  EXPECT_GE(corrected, -1.0f);
}

TEST(ControlLoopIntegrationTest, YawPid_Convergence_ClosedLoopDecayError) {
  // Simulate a closed-loop system with a first-order plant model:
  //   gz_next = gz + (plant_gain * corrected - gz) * (dt / plant_tau)
  // PI controller achieves zero steady-state error.
  PidController pid({.kp = 0.05f, .ki = 0.01f, .kd = 0.0f,
                     .max_integral = 5.0f, .max_output = 0.5f});
  const float commanded = 0.5f;
  const float steer_to_dps = 90.0f;
  const float plant_gain = 80.0f;  // dps per steering unit (steady state)
  const float plant_tau = 0.1f;    // 100ms first-order time constant

  float gz = 0.0f;
  const float dt = 0.002f;

  // Simulate 4 seconds (2000 steps at 500 Hz)
  for (int i = 0; i < 2000; ++i) {
    float corrected =
        RunStabTick(pid, commanded, gz, steer_to_dps, 1.0f, dt);
    // First-order plant dynamics: gz tracks plant_gain*corrected with tau
    gz += (plant_gain * corrected - gz) * (dt / plant_tau);
  }

  // After 4 seconds, PI controller should eliminate steady-state error
  const float desired_gz = steer_to_dps * commanded;
  EXPECT_NEAR(gz, desired_gz, 5.0f) << "PI controller should converge to desired yaw rate";
}

TEST(ControlLoopIntegrationTest, YawPid_ResetOnFailsafe_ClearsIntegral) {
  // When failsafe activates, PID should be reset to prevent windup
  PidController pid({.kp = 0.0f, .ki = 1.0f, .kd = 0.0f,
                     .max_integral = 10.0f, .max_output = 1.0f});
  const float dt = 0.002f;

  // Accumulate integral error
  for (int i = 0; i < 100; ++i) {
    pid.Step(50.0f, dt);  // Large constant error
  }
  EXPECT_GT(pid.GetIntegral(), 0.0f) << "Integral should have accumulated";

  // Failsafe activates → reset PID
  pid.Reset();
  EXPECT_FLOAT_EQ(pid.GetIntegral(), 0.0f) << "Integral should be zero after reset";

  // Next step should behave as first step (no D component)
  const float out = pid.Step(10.0f, dt);
  EXPECT_NEAR(out, 1.0f * (10.0f * dt), 1e-5f) << "After reset, only I contributes";
}

TEST(ControlLoopIntegrationTest, YawPid_ModePresets_SportHasHigherGain) {
  // Sport mode should produce stronger correction than normal mode
  // for the same error.
  StabilizationConfig normal{};
  normal.mode = 0;
  normal.ApplyModeDefaults();

  StabilizationConfig sport{};
  sport.mode = 1;
  sport.ApplyModeDefaults();

  PidController pid_normal({.kp = normal.pid_kp, .ki = normal.pid_ki,
                             .kd = normal.pid_kd,
                             .max_integral = normal.pid_max_integral,
                             .max_output = normal.pid_max_correction});
  PidController pid_sport({.kp = sport.pid_kp, .ki = sport.pid_ki,
                            .kd = sport.pid_kd,
                            .max_integral = sport.pid_max_integral,
                            .max_output = sport.pid_max_correction});

  const float error = 20.0f;  // 20 dps error
  const float dt = 0.002f;

  const float out_normal = pid_normal.Step(error, dt);
  const float out_sport = pid_sport.Step(error, dt);

  EXPECT_GT(std::abs(out_sport), std::abs(out_normal))
      << "Sport mode should produce stronger correction";
}

TEST(ControlLoopIntegrationTest, YawPid_SteeringSignPreserved_PositiveCmd) {
  // Positive steering + actual_gz too slow → correction should be positive
  PidController pid({.kp = 0.1f, .ki = 0.0f, .kd = 0.0f,
                     .max_integral = 1.0f, .max_output = 1.0f});
  const float corrected = RunStabTick(pid, 0.8f, 10.0f, 90.0f, 1.0f, 0.002f);
  // desired = 72 dps, actual = 10 dps → error = +62 → PID positive → increases steering
  EXPECT_GT(corrected, 0.8f);
  EXPECT_LE(corrected, 1.0f);
}

TEST(ControlLoopIntegrationTest, YawPid_SteeringSignPreserved_NegativeCmd) {
  // Negative steering + actual_gz too slow (less negative) → steering more negative
  PidController pid({.kp = 0.1f, .ki = 0.0f, .kd = 0.0f,
                     .max_integral = 1.0f, .max_output = 1.0f});
  const float corrected = RunStabTick(pid, -0.8f, -10.0f, 90.0f, 1.0f, 0.002f);
  // desired = -72 dps, actual = -10 dps → error = -62 → PID negative → more left
  EXPECT_LT(corrected, -0.8f);
  EXPECT_GE(corrected, -1.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Pitch Compensation Integration Tests
//
// Tests the pitch compensation logic:
//   pitch_deg → throttle correction (scaled by stab_weight)
// ═══════════════════════════════════════════════════════════════════════════

namespace {

// Simulate one pitch compensation tick: given commanded_throttle and pitch_deg,
// return the corrected throttle value.
float ApplyPitchComp(float commanded_throttle, float pitch_deg,
                     float pitch_comp_gain, float pitch_comp_max_correction,
                     float stab_weight) {
  float correction = pitch_comp_gain * pitch_deg;
  if (correction > pitch_comp_max_correction)
    correction = pitch_comp_max_correction;
  if (correction < -pitch_comp_max_correction)
    correction = -pitch_comp_max_correction;
  float result = commanded_throttle + correction * stab_weight;
  if (result > 1.0f) result = 1.0f;
  if (result < -1.0f) result = -1.0f;
  return result;
}

}  // namespace

TEST(ControlLoopIntegrationTest, PitchComp_ZeroPitch_NoCorrection) {
  // Flat surface → no throttle correction
  const float throttle = 0.5f;
  const float result =
      ApplyPitchComp(throttle, 0.0f, 0.01f, 0.25f, 1.0f);
  EXPECT_FLOAT_EQ(result, throttle);
}

TEST(ControlLoopIntegrationTest, PitchComp_PositivePitch_MoreThrottle) {
  // Nose up (uphill) → add throttle
  const float result = ApplyPitchComp(0.5f, 10.0f, 0.01f, 0.25f, 1.0f);
  // correction = 0.01 * 10 = 0.1 → throttle = 0.6
  EXPECT_FLOAT_EQ(result, 0.6f);
}

TEST(ControlLoopIntegrationTest, PitchComp_NegativePitch_LessThrottle) {
  // Nose down (downhill) → reduce throttle
  const float result = ApplyPitchComp(0.5f, -10.0f, 0.01f, 0.25f, 1.0f);
  // correction = 0.01 * (-10) = -0.1 → throttle = 0.4
  EXPECT_FLOAT_EQ(result, 0.4f);
}

TEST(ControlLoopIntegrationTest, PitchComp_ExceedsMax_ClampedToMax) {
  // Large pitch → correction capped at pitch_comp_max_correction
  // correction = 0.01 * 30 = 0.3 > 0.25 → capped to 0.25
  const float result = ApplyPitchComp(0.5f, 30.0f, 0.01f, 0.25f, 1.0f);
  EXPECT_FLOAT_EQ(result, 0.75f);
}

TEST(ControlLoopIntegrationTest, PitchComp_ExceedsMaxNeg_ClampedToNegMax) {
  // Large negative pitch → correction capped at -pitch_comp_max_correction
  const float result = ApplyPitchComp(0.5f, -30.0f, 0.01f, 0.25f, 1.0f);
  EXPECT_FLOAT_EQ(result, 0.25f);
}

TEST(ControlLoopIntegrationTest, PitchComp_ZeroWeight_NoEffect) {
  // stab_weight = 0 → no correction even if pitch is non-zero
  const float result = ApplyPitchComp(0.5f, 15.0f, 0.01f, 0.25f, 0.0f);
  EXPECT_FLOAT_EQ(result, 0.5f);
}

TEST(ControlLoopIntegrationTest, PitchComp_HalfWeight_HalfCorrection) {
  // stab_weight = 0.5 → correction * 0.5
  // correction = 0.01 * 10 = 0.1, scaled by 0.5 → 0.05
  const float result = ApplyPitchComp(0.5f, 10.0f, 0.01f, 0.25f, 0.5f);
  EXPECT_NEAR(result, 0.55f, 1e-5f);
}

TEST(ControlLoopIntegrationTest, PitchComp_ClampThrottleTo1) {
  // Even after adding correction, throttle must not exceed 1.0
  const float result = ApplyPitchComp(0.9f, 20.0f, 0.01f, 0.25f, 1.0f);
  EXPECT_LE(result, 1.0f);
}

TEST(ControlLoopIntegrationTest, PitchComp_ClampThrottleToNeg1) {
  // Even after subtracting correction, throttle must not go below -1.0
  const float result = ApplyPitchComp(-0.9f, -20.0f, 0.01f, 0.25f, 1.0f);
  EXPECT_GE(result, -1.0f);
}

TEST(ControlLoopIntegrationTest, PitchComp_ModeDefaultsGainDifference) {
  // Sport mode has higher gain than normal → stronger correction for same pitch
  StabilizationConfig normal{};
  normal.mode = 0;
  normal.ApplyModeDefaults();

  StabilizationConfig sport{};
  sport.mode = 1;
  sport.ApplyModeDefaults();

  const float pitch = 10.0f;
  const float result_normal = ApplyPitchComp(
      0.5f, pitch, normal.pitch_comp_gain, normal.pitch_comp_max_correction,
      1.0f);
  const float result_sport = ApplyPitchComp(
      0.5f, pitch, sport.pitch_comp_gain, sport.pitch_comp_max_correction,
      1.0f);

  EXPECT_GT(result_sport, result_normal)
      << "Sport mode should apply stronger pitch correction";
}