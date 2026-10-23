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
// Fixture: настраивает SlipAngleController в Drift mode
// ══════════════════════════════════════════════════════════════════════════════

class SlipAngleControllerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    imu_handler_.SetEnabled(true);

    cfg_.enabled = true;
    cfg_.mode = DriveMode::Drift;
    cfg_.slip_angle.pid.kp = 0.05f;
    cfg_.slip_angle.pid.ki = 0.0f;
    cfg_.slip_angle.pid.kd = 0.0f;
    cfg_.slip_angle.pid.max_integral = 0.5f;
    cfg_.slip_angle.pid.max_correction = 0.3f;
    cfg_.slip_angle.target_deg = 15.0f;  // Target 15° drift

    ctrl_.Init(cfg_, ekf_, &imu_handler_);
  }

  FakePlatform platform_;
  ImuCalibration calib_;
  MadgwickFilter madgwick_;
  ImuHandler imu_handler_{platform_, calib_, madgwick_};

  StabilizationConfig cfg_;
  VehicleEkf ekf_;
  SlipAngleController ctrl_;
};

// ══════════════════════════════════════════════════════════════════════════════
// Tests
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(SlipAngleControllerTest, WorksRegardlessOfMode_ModeFilteringIsDoneByTraits) {
  // After Strategy refactoring, mode filtering is done by ModeTraits in
  // control loop, not inside the controller. Controller is mode-agnostic.
  cfg_.mode = DriveMode::Normal;
  ctrl_.Init(cfg_, ekf_, &imu_handler_);
  ekf_.SetState(2.0f, 1.5f, 0.0f);
  float throttle = 0.5f;
  ctrl_.Process(cfg_, throttle, 1.0f, 1.0f, 2);
  EXPECT_NE(throttle, 0.5f)
      << "Controller processes regardless of mode; filtering is in control loop";
}

TEST_F(SlipAngleControllerTest, ActiveInDriftMode) {
  // EKF state: vx=2, vy=1.5 → slip≈36.9°, target=15°
  ekf_.SetState(2.0f, 1.5f, 0.0f);
  float throttle = 0.5f;
  ctrl_.Process(cfg_, throttle, 1.0f, 1.0f, 2);
  EXPECT_NE(throttle, 0.5f) << "Slip PID should be active in Drift mode";
}

// LOS-380: стик — предел газа. Ниже уставки регулятор не добавляет газ и не
// копит интеграл, иначе после заноса газ держался бы выше стика.
TEST_F(SlipAngleControllerTest, StickIsCeiling_WhenSlipBelowTarget) {
  cfg_.slip_angle.pid.ki = 0.05f;
  ctrl_.Init(cfg_, ekf_, &imu_handler_);
  ekf_.SetState(2.0f, 0.0f, 0.0f);  // slip 0°, target 15°
  float throttle = 0.5f;
  for (int i = 0; i < 100; ++i) {
    throttle = 0.5f;
    ctrl_.Process(cfg_, throttle, 1.0f, 1.0f, 2);
    EXPECT_FLOAT_EQ(throttle, 0.5f);
  }
  EXPECT_LE(ctrl_.GetPid().GetIntegral(), 0.0f);
}

TEST_F(SlipAngleControllerTest, NeverCutsBelowZero) {
  cfg_.slip_angle.pid.max_correction = 1.0f;
  ctrl_.Init(cfg_, ekf_, &imu_handler_);
  ekf_.SetState(2.0f, 2.0f, 0.0f);  // slip 45°
  float throttle = 0.1f;
  ctrl_.Process(cfg_, throttle, 1.0f, 1.0f, 2);
  EXPECT_FLOAT_EQ(throttle, 0.0f) << "Регулятор снимает газ, но не тормозит";
}

// Пока газ уже снят до нуля, интеграл не копит снятие дальше: иначе после
// заноса он ещё долго резал бы стик (anti-windup).
TEST_F(SlipAngleControllerTest, NoIntegralWindup_WhenCutHitsZero) {
  cfg_.slip_angle.pid.kp = 0.0f;
  cfg_.slip_angle.pid.ki = 0.02f;
  cfg_.slip_angle.pid.max_integral = 40.0f;
  cfg_.slip_angle.pid.max_correction = 0.8f;
  ctrl_.Init(cfg_, ekf_, &imu_handler_);
  ekf_.SetState(2.0f, 1.5f, 0.0f);  // slip ≈ 36.9°, ошибка ≈ −21.9°
  for (int i = 0; i < 500; ++i) {   // 1 с
    float throttle = 0.1f;
    ctrl_.Process(cfg_, throttle, 1.0f, 1.0f, 2);
  }
  // Снять 0.1 газа хватает интеграла ≈ −5; без anti-windup он дошёл бы
  // до ≈ −22.
  EXPECT_GT(ctrl_.GetPid().GetIntegral(), -6.0f);
}

TEST_F(SlipAngleControllerTest, BrakeAndReverseUntouched) {
  ekf_.SetState(2.0f, 1.5f, 0.0f);
  float throttle = -0.5f;
  ctrl_.Process(cfg_, throttle, 1.0f, 1.0f, 2);
  EXPECT_FLOAT_EQ(throttle, -0.5f);
}

TEST_F(SlipAngleControllerTest, DecreasesThrottle_WhenSlipAboveTarget) {
  // Slip = atan2(vy, vx): vx=2, vy=1.5 → slip ≈ 36.9°
  // target=15° → error = 15 - 36.9 = -21.9° → negative PID output
  ekf_.SetState(2.0f, 1.5f, 0.0f);
  float throttle = 0.5f;
  ctrl_.Process(cfg_, throttle, 1.0f, 1.0f, 2);
  EXPECT_LT(throttle, 0.5f)
      << "Slip above target → decrease throttle to reduce slip";
}

TEST_F(SlipAngleControllerTest, NoEffect_WhenStabWeightZero) {
  ekf_.SetState(2.0f, 0.0f, 0.0f);
  float throttle = 0.5f;
  ctrl_.Process(cfg_, throttle, 0.0f, 1.0f, 2);
  EXPECT_FLOAT_EQ(throttle, 0.5f) << "stab_w=0 → no correction";
}

TEST_F(SlipAngleControllerTest, NoEffect_WhenModeWeightZero) {
  ekf_.SetState(2.0f, 0.0f, 0.0f);
  float throttle = 0.5f;
  ctrl_.Process(cfg_, throttle, 1.0f, 0.0f, 2);
  EXPECT_NEAR(throttle, 0.5f, 0.01f) << "mode_w=0 → correction * 0 = 0";
}

TEST_F(SlipAngleControllerTest, NoEffect_WhenImuDisabled) {
  imu_handler_.SetEnabled(false);
  ekf_.SetState(2.0f, 0.0f, 0.0f);
  float throttle = 0.5f;
  ctrl_.Process(cfg_, throttle, 1.0f, 1.0f, 2);
  EXPECT_FLOAT_EQ(throttle, 0.5f) << "No correction when IMU disabled";
}

TEST_F(SlipAngleControllerTest, NoEffect_WhenDtZero) {
  ekf_.SetState(2.0f, 0.0f, 0.0f);
  float throttle = 0.5f;
  ctrl_.Process(cfg_, throttle, 1.0f, 1.0f, 0);
  EXPECT_FLOAT_EQ(throttle, 0.5f) << "dt=0 → early return";
}

TEST_F(SlipAngleControllerTest, ThrottleClamped_ToMinusOnePlusOne) {
  ekf_.SetState(2.0f, 0.0f, 0.0f);
  float throttle = 0.9f;
  for (int i = 0; i < 50; ++i) {
    ctrl_.Process(cfg_, throttle, 1.0f, 1.0f, 2);
  }
  EXPECT_LE(throttle, 1.0f);
  EXPECT_GE(throttle, -1.0f);
}

TEST_F(SlipAngleControllerTest, CorrectionScaledByModeWeight) {
  ekf_.SetState(2.0f, 1.5f, 0.0f);

  float throttle1 = 0.5f;
  ctrl_.Process(cfg_, throttle1, 1.0f, 1.0f, 2);
  float correction_full = throttle1 - 0.5f;

  ctrl_.Reset();
  float throttle2 = 0.5f;
  ctrl_.Process(cfg_, throttle2, 1.0f, 0.5f, 2);
  float correction_half = throttle2 - 0.5f;

  ASSERT_LT(correction_full, -0.001f);
  EXPECT_NEAR(correction_half, correction_full * 0.5f, 0.01f)
      << "Correction should scale with mode_w";
}

TEST_F(SlipAngleControllerTest, SetGains_UpdatesPidCoefficients) {
  StabilizationConfig new_cfg = cfg_;
  new_cfg.slip_angle.pid.kp = 0.2f;
  new_cfg.slip_angle.pid.ki = 0.05f;
  ctrl_.SetGains(new_cfg);
  auto gains = ctrl_.GetPid().GetGains();
  EXPECT_FLOAT_EQ(gains.kp, 0.2f);
  EXPECT_FLOAT_EQ(gains.ki, 0.05f);
}

TEST_F(SlipAngleControllerTest, Reset_ClearsPidState) {
  ekf_.SetState(2.0f, 0.0f, 0.0f);
  float throttle = 0.5f;
  for (int i = 0; i < 20; ++i) {
    ctrl_.Process(cfg_, throttle, 1.0f, 1.0f, 2);
  }
  ctrl_.Reset();
  EXPECT_FLOAT_EQ(ctrl_.GetPid().GetIntegral(), 0.0f);
}

TEST_F(SlipAngleControllerTest, ZeroTargetSlip_CutsOnlyOutwardSlip) {
  cfg_.slip_angle.target_deg = 0.0f;  // Target: no drift at all
  ctrl_.Init(cfg_, ekf_, &imu_handler_);

  // Правый поворот (r < 0): slip > 0 — занос, зад выносит наружу.
  ekf_.SetState(2.0f, 0.5f, -1.0f);
  float throttle_drift = 0.5f;
  ctrl_.Process(cfg_, throttle_drift, 1.0f, 1.0f, 2);

  // Тот же поворот, slip < 0 — машину сносит внутрь, заноса нет.
  ctrl_.Reset();
  ekf_.SetState(2.0f, -0.5f, -1.0f);
  float throttle_inside = 0.5f;
  ctrl_.Process(cfg_, throttle_inside, 1.0f, 1.0f, 2);

  EXPECT_LT(throttle_drift, 0.5f);
  EXPECT_FLOAT_EQ(throttle_inside, 0.5f);
}

// LOS-380: занос в левом повороте даёт slip < 0. Со знаковым slip ошибка
// уставки росла с заносом, и PID добавлял газ до разворота.
TEST_F(SlipAngleControllerTest, LeftTurnDriftAboveTarget_DecreasesThrottle) {
  ekf_.SetState(2.0f, -1.5f, 1.0f);  // slip ≈ −36.9°, r > 0 — левый поворот
  float throttle = 0.5f;
  ctrl_.Process(cfg_, throttle, 1.0f, 1.0f, 2);
  EXPECT_LT(throttle, 0.5f);
}

TEST_F(SlipAngleControllerTest, LeftAndRightDriftGiveSameCorrection) {
  ekf_.SetState(2.0f, -1.0f, 1.0f);  // левый поворот, занос ≈ 26.6°
  float left = 0.5f;
  ctrl_.Process(cfg_, left, 1.0f, 1.0f, 2);

  ctrl_.Reset();
  ekf_.SetState(2.0f, 1.0f, -1.0f);  // правый поворот, занос ≈ 26.6°
  float right = 0.5f;
  ctrl_.Process(cfg_, right, 1.0f, 1.0f, 2);

  EXPECT_LT(left, 0.5f);  // выше уставки 15° — снять газ
  EXPECT_FLOAT_EQ(left, right);
}

// Без поворота направление заноса не определено: любой угол — занос, и PID
// не разгоняет машину, которую сносит боком на прямой.
TEST_F(SlipAngleControllerTest, NoTurn_AnySlipCountsAsDrift) {
  EXPECT_FLOAT_EQ(SlipAngleController::DriftSlipDeg(-20.0f, 0.1f), 20.0f);
  EXPECT_FLOAT_EQ(SlipAngleController::DriftSlipDeg(20.0f, -0.1f), 20.0f);
  EXPECT_FLOAT_EQ(SlipAngleController::DriftSlipDeg(-20.0f, 1.0f), 20.0f);
  EXPECT_FLOAT_EQ(SlipAngleController::DriftSlipDeg(-20.0f, -1.0f), -20.0f);
}

TEST_F(SlipAngleControllerTest, NoCorrection_WhenAtTarget) {
  // Set EKF state to produce slip ≈ target (15°)
  // slip = atan2(vy, vx) = 15° → vy = vx * tan(15°)
  const float vx = 2.0f;
  const float vy = vx * std::tan(15.0f * static_cast<float>(M_PI) / 180.0f);
  ekf_.SetState(vx, vy, 0.0f);

  float throttle = 0.5f;
  ctrl_.Process(cfg_, throttle, 1.0f, 1.0f, 2);
  EXPECT_NEAR(throttle, 0.5f, 0.02f)
      << "When slip ≈ target, correction should be near zero";
}
