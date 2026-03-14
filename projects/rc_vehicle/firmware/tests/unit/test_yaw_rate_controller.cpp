#include <gtest/gtest.h>

#include <cmath>

#include "control_components.hpp"
#include "imu_calibration.hpp"
#include "madgwick_filter.hpp"
#include "mock_platform.hpp"
#include "stabilization_config.hpp"
#include "stabilization_pipeline.hpp"
#include "vehicle_ekf.hpp"

using namespace rc_vehicle;
using namespace rc_vehicle::testing;

// ══════════════════════════════════════════════════════════════════════════════
// Fixture: настраивает YawRateController с Normal mode, IMU enabled
// ══════════════════════════════════════════════════════════════════════════════

class YawRateControllerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // IMU data: flat, no rotation — accel = 1g down, gyro = 0
    ImuData flat_data{};
    flat_data.ax = 0.0f;
    flat_data.ay = 0.0f;
    flat_data.az = 1.0f;  // 1 g down
    flat_data.gx = 0.0f;
    flat_data.gy = 0.0f;
    flat_data.gz = 0.0f;
    platform_.SetImuData(flat_data);

    imu_handler_.SetEnabled(true);
    // Warm up IMU handler (LPF convergence) — feed constant data
    WarmUpImu(100);

    cfg_.enabled = true;
    cfg_.mode = DriveMode::Normal;
    cfg_.yaw_rate.pid.kp = 0.1f;
    cfg_.yaw_rate.pid.ki = 0.0f;
    cfg_.yaw_rate.pid.kd = 0.0f;
    cfg_.yaw_rate.pid.max_integral = 0.5f;
    cfg_.yaw_rate.pid.max_correction = 0.3f;
    cfg_.yaw_rate.steer_to_yaw_rate_dps = 90.0f;
    cfg_.adaptive.enabled = false;

    ctrl_.Init(cfg_, ekf_, &imu_handler_);
  }

  /// Feed constant IMU data N times (2 ms apart) to let LPF converge
  void WarmUpImu(int n) {
    for (int i = 0; i < n; ++i) {
      platform_.AdvanceTimeMs(2);
      imu_handler_.Update(platform_.GetTimeMs(), 2);
    }
  }

  /// Set gyro Z on FakePlatform and warm up to let LPF converge
  void SetGyroZ(float gz_dps, int warmup = 50) {
    ImuData data{};
    data.az = 1.0f;
    data.gz = gz_dps;
    platform_.SetImuData(data);
    WarmUpImu(warmup);
  }

  FakePlatform platform_;
  ImuCalibration calib_;
  MadgwickFilter madgwick_;
  ImuHandler imu_handler_{platform_, calib_, madgwick_};

  StabilizationConfig cfg_;
  VehicleEkf ekf_;
  YawRateController ctrl_;
};

// ══════════════════════════════════════════════════════════════════════════════
// Tests
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(YawRateControllerTest, NoCorrection_WhenSteeringZero_AndGyroZero) {
  // steering=0 → desired omega=0, actual omega=0 → error=0 → no correction
  float steering = 0.0f;
  ctrl_.Process(steering, 1.0f, 1.0f, 2);
  EXPECT_NEAR(steering, 0.0f, 0.01f);
}

TEST_F(YawRateControllerTest, PositiveCorrection_WhenDesiredOmegaExceedsActual) {
  // steering=0.5 → desired omega = 90 * 0.5 = 45 dps
  // actual omega ≈ 0 (gyro Z = 0)
  // error = 45 → PID output = kp * 45 = 4.5, clamped to max_correction=0.3
  // steering += 0.3 * stab_w * mode_w = 0.3
  float steering = 0.5f;
  ctrl_.Process(steering, 1.0f, 1.0f, 2);
  EXPECT_GT(steering, 0.5f) << "Steering should increase when desired > actual yaw rate";
  EXPECT_LE(steering, 1.0f) << "Steering clamped to [-1, 1]";
}

TEST_F(YawRateControllerTest, NegativeCorrection_WhenActualExceedsDesired) {
  // Set gyro Z to 90 dps, but steering=0 → desired=0, actual=90 → error=-90
  SetGyroZ(90.0f);
  float steering = 0.0f;
  ctrl_.Process(steering, 1.0f, 1.0f, 2);
  EXPECT_LT(steering, 0.0f) << "Steering should decrease when actual > desired yaw rate";
}

TEST_F(YawRateControllerTest, NoEffect_WhenStabWeightZero) {
  float steering = 0.5f;
  ctrl_.Process(steering, 0.0f, 1.0f, 2);
  EXPECT_FLOAT_EQ(steering, 0.5f) << "stab_w=0 → no correction";
}

TEST_F(YawRateControllerTest, NoEffect_WhenModeWeightZero) {
  // mode_w=0 → pid_out * 0 = 0
  float steering = 0.5f;
  ctrl_.Process(steering, 1.0f, 0.0f, 2);
  // PID output is non-zero but multiplied by mode_w=0
  // steering = clamp(0.5 + correction * 1.0 * 0.0, -1, 1) = 0.5
  EXPECT_NEAR(steering, 0.5f, 0.01f);
}

TEST_F(YawRateControllerTest, NoEffect_InDriftMode) {
  cfg_.mode = DriveMode::Drift;
  ctrl_.Init(cfg_, ekf_, &imu_handler_);
  float steering = 0.5f;
  ctrl_.Process(steering, 1.0f, 1.0f, 2);
  EXPECT_FLOAT_EQ(steering, 0.5f) << "Yaw PID disabled in Drift mode";
}

TEST_F(YawRateControllerTest, NoEffect_WhenImuDisabled) {
  imu_handler_.SetEnabled(false);
  float steering = 0.5f;
  ctrl_.Process(steering, 1.0f, 1.0f, 2);
  EXPECT_FLOAT_EQ(steering, 0.5f) << "No correction when IMU disabled";
}

TEST_F(YawRateControllerTest, NoEffect_WhenDtZero) {
  float steering = 0.5f;
  ctrl_.Process(steering, 1.0f, 1.0f, 0);
  EXPECT_FLOAT_EQ(steering, 0.5f) << "dt=0 → early return";
}

TEST_F(YawRateControllerTest, WorksInSportMode) {
  cfg_.mode = DriveMode::Sport;
  ctrl_.Init(cfg_, ekf_, &imu_handler_);
  float steering = 0.5f;
  ctrl_.Process(steering, 1.0f, 1.0f, 2);
  EXPECT_NE(steering, 0.5f) << "Yaw PID should work in Sport mode";
}

TEST_F(YawRateControllerTest, CorrectionScaledByStabWeight) {
  // Process with stab_w=1.0
  float steering1 = 0.5f;
  ctrl_.Process(steering1, 1.0f, 1.0f, 2);
  float correction_full = steering1 - 0.5f;

  // Reset PID and process with stab_w=0.5
  ctrl_.Reset();
  float steering2 = 0.5f;
  ctrl_.Process(steering2, 0.5f, 1.0f, 2);
  float correction_half = steering2 - 0.5f;

  EXPECT_NEAR(correction_half, correction_full * 0.5f, 0.02f)
      << "Correction should scale linearly with stab_w";
}

TEST_F(YawRateControllerTest, SteeringClampedToMinusOnePlusOne) {
  // Large error to saturate PID → steering should not exceed ±1
  SetGyroZ(-200.0f);  // Large negative → correction pushes steering up
  float steering = 0.9f;
  for (int i = 0; i < 10; ++i) {
    ctrl_.Process(steering, 1.0f, 1.0f, 2);
  }
  EXPECT_LE(steering, 1.0f);
  EXPECT_GE(steering, -1.0f);
}

TEST_F(YawRateControllerTest, SetGains_UpdatesPidCoefficients) {
  StabilizationConfig new_cfg = cfg_;
  new_cfg.yaw_rate.pid.kp = 0.5f;
  new_cfg.yaw_rate.pid.ki = 0.01f;
  ctrl_.SetGains(new_cfg);
  auto gains = ctrl_.GetPid().GetGains();
  EXPECT_FLOAT_EQ(gains.kp, 0.5f);
  EXPECT_FLOAT_EQ(gains.ki, 0.01f);
}

TEST_F(YawRateControllerTest, Reset_ClearsPidState) {
  // Accumulate some integral
  float steering = 0.5f;
  for (int i = 0; i < 10; ++i) {
    ctrl_.Process(steering, 1.0f, 1.0f, 2);
  }
  ctrl_.Reset();
  EXPECT_FLOAT_EQ(ctrl_.GetPid().GetIntegral(), 0.0f);
}

TEST_F(YawRateControllerTest, AdaptivePid_ScalesByEkfSpeed) {
  cfg_.adaptive.enabled = true;
  cfg_.adaptive.speed_ref_ms = 1.0f;
  cfg_.adaptive.scale_min = 0.5f;
  cfg_.adaptive.scale_max = 2.0f;
  ctrl_.Init(cfg_, ekf_, &imu_handler_);

  // EKF speed = 0 → scale = clamp(0/1, 0.5, 2.0) = 0.5
  ekf_.SetState(0.0f, 0.0f, 0.0f);
  float steering_slow = 0.5f;
  ctrl_.Process(steering_slow, 1.0f, 1.0f, 2);
  float correction_slow = steering_slow - 0.5f;

  // EKF speed = 2.0 → scale = clamp(2/1, 0.5, 2.0) = 2.0
  ctrl_.Reset();
  ekf_.SetState(2.0f, 0.0f, 0.0f);
  float steering_fast = 0.5f;
  ctrl_.Process(steering_fast, 1.0f, 1.0f, 2);
  float correction_fast = steering_fast - 0.5f;

  // correction_fast should be ~4x correction_slow (scale 2.0 vs 0.5)
  if (std::abs(correction_slow) > 0.001f) {
    EXPECT_GT(std::abs(correction_fast), std::abs(correction_slow))
        << "Higher EKF speed → larger adaptive scale → larger correction";
  }
}

TEST_F(YawRateControllerTest, AdaptivePid_Disabled_NoScaling) {
  cfg_.adaptive.enabled = false;
  ctrl_.Init(cfg_, ekf_, &imu_handler_);

  ekf_.SetState(0.0f, 0.0f, 0.0f);
  float steering1 = 0.5f;
  ctrl_.Process(steering1, 1.0f, 1.0f, 2);

  ctrl_.Reset();
  ekf_.SetState(5.0f, 0.0f, 0.0f);
  float steering2 = 0.5f;
  ctrl_.Process(steering2, 1.0f, 1.0f, 2);

  EXPECT_FLOAT_EQ(steering1, steering2)
      << "Without adaptive PID, speed should not affect correction";
}
