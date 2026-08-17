#include <gtest/gtest.h>

#include <cmath>

#include "vehicle_state_estimator.hpp"
#ifdef RC_PROFILE_LOOP
#include "mock_platform.hpp"
#endif

namespace rc_vehicle {
namespace {

class VehicleStateEstimatorTest : public ::testing::Test {
 protected:
  VehicleStateEstimatorTest() : estimator_(calib_, madgwick_, ekf_) {
    input_.filter.ekf_enabled = true;
    input_.filter.tilt_comp_enabled = true;
    input_.filter.madgwick_enabled = true;
    input_.dt_ms = 2;
    input_.ekf_available = true;

    sensors_.imu_enabled = true;
    sensors_.imu_data.az = 1.0f;
  }

  ImuCalibration calib_;
  MadgwickFilter madgwick_;
  VehicleEkf ekf_;
  VehicleStateEstimator estimator_;
  SensorSnapshot sensors_;
  VehicleStateEstimatorInput input_;
};

TEST_F(VehicleStateEstimatorTest, NoImuReturnsInvalidZeroEstimate) {
  sensors_.imu_enabled = false;

  const auto estimate = estimator_.Update(sensors_, input_);

  EXPECT_FALSE(estimate.imu_valid);
  EXPECT_FALSE(estimate.tilt_valid);
  EXPECT_FLOAT_EQ(estimate.forward_accel_g, 0.0f);
  EXPECT_FLOAT_EQ(estimate.speed_ms, 0.0f);
}

TEST_F(VehicleStateEstimatorTest, ZeroDtDoesNotAdvanceTiltOrEkf) {
  input_.dt_ms = 0;
  sensors_.imu_data.ax = 0.4f;

  const auto estimate = estimator_.Update(sensors_, input_);

  EXPECT_TRUE(estimate.imu_valid);
  EXPECT_FALSE(estimate.tilt_valid);
  EXPECT_FLOAT_EQ(estimate.speed_ms, 0.0f);
}

TEST_F(VehicleStateEstimatorTest, LevelStationaryProducesStableState) {
  VehicleStateEstimate estimate;
  for (int i = 0; i < 500; ++i) {
    estimate = estimator_.Update(sensors_, input_);
  }

  EXPECT_TRUE(estimate.imu_valid);
  EXPECT_TRUE(estimate.tilt_valid);
  EXPECT_NEAR(estimate.pitch_rad, 0.0f, 1e-4f);
  EXPECT_NEAR(estimate.roll_rad, 0.0f, 1e-4f);
  EXPECT_NEAR(estimate.speed_ms, 0.0f, 1e-3f);
  EXPECT_NEAR(estimate.forward_accel_g, 0.0f, 1e-4f);
  EXPECT_EQ(estimate.zupt_status, ZuptStatus::Applied);
  EXPECT_FALSE(estimate.ekf_diverged);
}

TEST_F(VehicleStateEstimatorTest, SubDeadzoneThrottleResidualAllowsZupt) {
  input_.filter.motor_deadzone = 0.05f;
  input_.applied_throttle = 0.049f;

  const auto estimate = estimator_.Update(sensors_, input_);

  EXPECT_EQ(estimate.zupt_status, ZuptStatus::Applied);
}

TEST_F(VehicleStateEstimatorTest, MotionCapableThrottleRejectsZupt) {
  input_.filter.motor_deadzone = 0.05f;
  input_.applied_throttle = 0.051f;

  const auto estimate = estimator_.Update(sensors_, input_);

  EXPECT_EQ(estimate.zupt_status, ZuptStatus::ThrottleRejected);
}

TEST_F(VehicleStateEstimatorTest, TiltRunsWhenEkfIsDisabled) {
  input_.filter.ekf_enabled = false;
  sensors_.imu_data.ax = -0.17364818f;
  sensors_.imu_data.az = 0.98480775f;

  VehicleStateEstimate estimate;
  for (int i = 0; i < 3000; ++i) {
    estimate = estimator_.Update(sensors_, input_);
  }

  EXPECT_TRUE(estimate.tilt_valid);
  EXPECT_GT(estimate.pitch_rad, 0.1f);
  EXPECT_NEAR(estimate.speed_ms, 0.0f, 1e-5f);
  EXPECT_NEAR(estimate.forward_accel_g, 0.0f, 0.03f);
}

TEST_F(VehicleStateEstimatorTest, MagnetometerHeadingUpdatesEkfYaw) {
  sensors_.mag_enabled = true;
  sensors_.heading_deg = 90.0f;

  VehicleStateEstimate estimate;
  for (int i = 0; i < 20; ++i) {
    sensors_.mag_sample_sequence = static_cast<uint32_t>(i + 1);
    estimate = estimator_.Update(sensors_, input_);
  }

  EXPECT_NEAR(estimate.yaw_rad, 3.14159265f / 2.0f, 0.05f);
}

TEST_F(VehicleStateEstimatorTest, AppliesEachMagSampleOnlyOnce) {
  sensors_.mag_enabled = true;
  sensors_.heading_deg = 90.0f;
  sensors_.mag_sample_sequence = 1;

  const auto first = estimator_.Update(sensors_, input_);
  VehicleStateEstimate duplicate = first;
  for (int i = 0; i < 20; ++i) {
    duplicate = estimator_.Update(sensors_, input_);
  }
  EXPECT_NEAR(duplicate.yaw_rad, first.yaw_rad, 1e-5f);

  sensors_.mag_sample_sequence = 2;
  const auto fresh = estimator_.Update(sensors_, input_);
  EXPECT_GT(fresh.yaw_rad, duplicate.yaw_rad);
}

TEST_F(VehicleStateEstimatorTest, RejectedMagSampleDoesNotUpdateEkfYaw) {
  sensors_.mag_enabled = true;
  sensors_.mag_rejected = true;
  sensors_.heading_deg = 90.0f;
  sensors_.mag_sample_sequence = 1;

  const auto estimate = estimator_.Update(sensors_, input_);

  EXPECT_NEAR(estimate.yaw_rad, 0.0f, 1e-6f);
}

TEST_F(VehicleStateEstimatorTest, MotorModelAnchorsForwardSpeed) {
  input_.filter.motor_model_enabled = true;
  input_.filter.motor_deadzone = 0.1f;
  input_.filter.motor_speed_gain = 5.0f;
  input_.filter.speed_meas_noise = 0.1f;
  input_.applied_throttle = 0.5f;
  input_.motor_model_throttle = 0.5f;

  VehicleStateEstimate estimate;
  for (int i = 0; i < 100; ++i) {
    estimate = estimator_.Update(sensors_, input_);
  }

  EXPECT_GT(estimate.vx_ms, 1.0f);
  EXPECT_LT(estimate.vx_ms, VehicleEkf::kMaxSpeedMs);
}

TEST_F(VehicleStateEstimatorTest, FrameChangeResetsTiltHistory) {
  sensors_.imu_data.ax = -0.5f;
  sensors_.imu_data.az = 0.8660254f;
  for (int i = 0; i < 500; ++i) {
    (void)estimator_.Update(sensors_, input_);
  }

  estimator_.OnReferenceFrameChanged();
  sensors_.imu_data.ax = 0.0f;
  sensors_.imu_data.az = 1.0f;
  const auto estimate = estimator_.Update(sensors_, input_);

  EXPECT_TRUE(estimate.tilt_valid);
  EXPECT_NEAR(estimate.pitch_rad, 0.0f, 1e-3f);
}

TEST_F(VehicleStateEstimatorTest, RefreshEkfFieldsDropsPreResetState) {
  VehicleStateEstimate estimate;
  estimate.pitch_rad = 0.25f;
  estimate.forward_accel_g = 0.4f;
  ekf_.SetState(4.0f, 3.0f, 0.5f);
  estimator_.RefreshEkfFields(estimate);
  ASSERT_FLOAT_EQ(estimate.speed_ms, 5.0f);

  ekf_.Reset();
  estimator_.RefreshEkfFields(estimate);

  EXPECT_FLOAT_EQ(estimate.vx_ms, 0.0f);
  EXPECT_FLOAT_EQ(estimate.vy_ms, 0.0f);
  EXPECT_FLOAT_EQ(estimate.speed_ms, 0.0f);
  EXPECT_FLOAT_EQ(estimate.yaw_rate_rps, 0.0f);
  EXPECT_FLOAT_EQ(estimate.pitch_rad, 0.25f);
  EXPECT_FLOAT_EQ(estimate.forward_accel_g, 0.4f);
}

#ifdef RC_PROFILE_LOOP
TEST_F(VehicleStateEstimatorTest, ProfileRecordsConditionalStagesAndResets) {
  testing::FakePlatform clock;
  clock.SetTimeUsReadIncrement(1);
  input_.filter.motor_model_enabled = true;
  input_.filter.nhc_enabled = true;
  sensors_.mag_enabled = true;
  sensors_.mag_sample_sequence = 1;

  (void)estimator_.Update(sensors_, input_, &clock);

  const auto& profile = estimator_.GetProfileStats();
  EXPECT_EQ(profile.com_offset.calls, 1u);
  EXPECT_EQ(profile.rotate.calls, 1u);
  EXPECT_EQ(profile.tilt.calls, 1u);
  EXPECT_EQ(profile.imu.calls, 1u);
  EXPECT_EQ(profile.speed.calls, 1u);
  EXPECT_EQ(profile.nhc.calls, 1u);
  EXPECT_EQ(profile.heading.calls, 1u);
  EXPECT_EQ(profile.com_offset.total_us, 1u);
  EXPECT_EQ(profile.heading.max_us, 1u);

  estimator_.ResetProfileStats();

  EXPECT_EQ(estimator_.GetProfileStats().com_offset.calls, 0u);
  EXPECT_EQ(estimator_.GetProfileStats().heading.total_us, 0u);
}

TEST_F(VehicleStateEstimatorTest, ProfileDoesNotCountSkippedStages) {
  testing::FakePlatform clock;
  clock.SetTimeUsReadIncrement(1);
  sensors_.imu_enabled = false;

  (void)estimator_.Update(sensors_, input_, &clock);

  const auto& profile = estimator_.GetProfileStats();
  EXPECT_EQ(profile.com_offset.calls, 1u);
  EXPECT_EQ(profile.rotate.calls, 0u);
  EXPECT_EQ(profile.tilt.calls, 0u);
  EXPECT_EQ(profile.imu.calls, 0u);
  EXPECT_EQ(profile.speed.calls, 0u);
  EXPECT_EQ(profile.nhc.calls, 0u);
  EXPECT_EQ(profile.heading.calls, 0u);
}
#endif

}  // namespace
}  // namespace rc_vehicle
