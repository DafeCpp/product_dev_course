#include <gtest/gtest.h>

#include <cmath>

#include "imu_calibration.hpp"

namespace rc_vehicle {
namespace {

class ComOffsetCorrectionTest : public ::testing::Test {
 protected:
  ImuCalibration calib;
  static constexpr float kG = 9.80665f;
  static constexpr float kDegToRad = 3.14159265358979f / 180.0f;

  void SetUp() override {
    ImuCalibData data{};
    data.valid = true;
    calib.SetData(data);
  }

  void SetComOffset(float rx, float ry) {
    auto data = calib.GetData();
    data.com_offset[0] = rx;
    data.com_offset[1] = ry;
    calib.SetData(data);
  }
};

TEST_F(ComOffsetCorrectionTest, ZeroOffsetNoChange) {
  SetComOffset(0.0f, 0.0f);
  ImuData imu{};
  imu.ax = 0.5f;
  imu.ay = -0.3f;
  calib.CorrectForComOffset(imu, 5.0f, 1.0f);
  EXPECT_FLOAT_EQ(imu.ax, 0.5f);
  EXPECT_FLOAT_EQ(imu.ay, -0.3f);
}

TEST_F(ComOffsetCorrectionTest, CentripetalCorrectionX) {
  // rx=0.05m, ω=10 rad/s, α=0
  // correction_ax = ω²·rx / g = 100 * 0.05 / 9.80665 ≈ 0.5099
  SetComOffset(0.05f, 0.0f);
  ImuData imu{};
  imu.ax = 0.0f;
  imu.ay = 0.0f;
  calib.CorrectForComOffset(imu, 10.0f, 0.0f);
  float expected_ax = (100.0f * 0.05f) / kG;
  EXPECT_NEAR(imu.ax, expected_ax, 1e-5f);
  EXPECT_NEAR(imu.ay, 0.0f, 1e-5f);
}

TEST_F(ComOffsetCorrectionTest, CentripetalCorrectionY) {
  // ry=0.03m, ω=10 rad/s, α=0
  // correction_ay = ω²·ry / g = 100 * 0.03 / 9.80665
  SetComOffset(0.0f, 0.03f);
  ImuData imu{};
  imu.ax = 0.0f;
  imu.ay = 0.0f;
  calib.CorrectForComOffset(imu, 10.0f, 0.0f);
  float expected_ay = (100.0f * 0.03f) / kG;
  EXPECT_NEAR(imu.ax, 0.0f, 1e-5f);
  EXPECT_NEAR(imu.ay, expected_ay, 1e-5f);
}

TEST_F(ComOffsetCorrectionTest, TangentialCorrectionFromAlpha) {
  // rx=0.05m, ry=0.03m, ω=0, α=20 rad/s²
  // correction_ax = α·ry / g = 20 * 0.03 / 9.80665
  // correction_ay = -α·rx / g = -20 * 0.05 / 9.80665
  SetComOffset(0.05f, 0.03f);
  ImuData imu{};
  imu.ax = 0.0f;
  imu.ay = 0.0f;
  calib.CorrectForComOffset(imu, 0.0f, 20.0f);
  float expected_ax = (20.0f * 0.03f) / kG;
  float expected_ay = -(20.0f * 0.05f) / kG;
  EXPECT_NEAR(imu.ax, expected_ax, 1e-5f);
  EXPECT_NEAR(imu.ay, expected_ay, 1e-5f);
}

TEST_F(ComOffsetCorrectionTest, CombinedCentripetalAndTangential) {
  // rx=0.05m, ry=0.03m, ω=10 rad/s, α=5 rad/s²
  // correction_ax = (ω²·rx + α·ry) / g = (100*0.05 + 5*0.03) / 9.80665
  // correction_ay = (ω²·ry - α·rx) / g = (100*0.03 - 5*0.05) / 9.80665
  SetComOffset(0.05f, 0.03f);
  ImuData imu{};
  imu.ax = 1.0f;
  imu.ay = -0.5f;
  calib.CorrectForComOffset(imu, 10.0f, 5.0f);

  float corr_ax = (100.0f * 0.05f + 5.0f * 0.03f) / kG;
  float corr_ay = (100.0f * 0.03f - 5.0f * 0.05f) / kG;
  EXPECT_NEAR(imu.ax, 1.0f + corr_ax, 1e-5f);
  EXPECT_NEAR(imu.ay, -0.5f + corr_ay, 1e-5f);
}

TEST_F(ComOffsetCorrectionTest, NegativeOffset) {
  // rx=-0.04m, ry=0, ω=8 rad/s, α=0
  // correction_ax = ω²·rx / g = 64 * (-0.04) / 9.80665 (negative correction)
  SetComOffset(-0.04f, 0.0f);
  ImuData imu{};
  imu.ax = 0.0f;
  calib.CorrectForComOffset(imu, 8.0f, 0.0f);
  float expected_ax = (64.0f * -0.04f) / kG;
  EXPECT_NEAR(imu.ax, expected_ax, 1e-5f);
}

TEST_F(ComOffsetCorrectionTest, NegativeOmega) {
  // ω² is always positive regardless of sign of ω
  SetComOffset(0.05f, 0.0f);
  ImuData imu_pos{}, imu_neg{};
  calib.CorrectForComOffset(imu_pos, 10.0f, 0.0f);
  calib.CorrectForComOffset(imu_neg, -10.0f, 0.0f);
  // Centripetal correction should be identical for ±ω
  EXPECT_FLOAT_EQ(imu_pos.ax, imu_neg.ax);
}

TEST_F(ComOffsetCorrectionTest, NegativeAlpha) {
  // α < 0 should give opposite tangential correction
  SetComOffset(0.05f, 0.03f);
  ImuData imu_pos{}, imu_neg{};
  calib.CorrectForComOffset(imu_pos, 0.0f, 10.0f);
  calib.CorrectForComOffset(imu_neg, 0.0f, -10.0f);
  // Tangential: α·ry → opposite sign for opposite α
  EXPECT_NEAR(imu_pos.ax, -imu_neg.ax, 1e-6f);
  EXPECT_NEAR(imu_pos.ay, -imu_neg.ay, 1e-6f);
}

TEST_F(ComOffsetCorrectionTest, TypicalRcVehicleScenario) {
  // Typical: rx=0.02m (2cm offset), ω=50 dps turning, α≈0
  SetComOffset(0.02f, 0.0f);
  ImuData imu{};
  imu.ax = 0.1f;  // 0.1g forward accel
  imu.ay = 0.0f;

  float omega_rad = 50.0f * kDegToRad;  // ~0.873 rad/s
  calib.CorrectForComOffset(imu, omega_rad, 0.0f);

  // correction_ax = ω²·rx / g = 0.762 * 0.02 / 9.807 ≈ 0.00155g
  float corr = (omega_rad * omega_rad * 0.02f) / kG;
  EXPECT_NEAR(imu.ax, 0.1f + corr, 1e-5f);
  // Small correction for typical RC vehicle offset
  EXPECT_LT(corr, 0.01f);
}

TEST_F(ComOffsetCorrectionTest, HighSpeedLargeCorrection) {
  // Extreme: rx=0.1m, ω=300 dps (fast spin)
  SetComOffset(0.1f, 0.0f);
  ImuData imu{};

  float omega_rad = 300.0f * kDegToRad;  // ~5.24 rad/s
  calib.CorrectForComOffset(imu, omega_rad, 0.0f);

  // correction_ax = ω²·rx / g = 27.4 * 0.1 / 9.807 ≈ 0.28g
  float corr = (omega_rad * omega_rad * 0.1f) / kG;
  EXPECT_NEAR(imu.ax, corr, 1e-4f);
  // Significant correction at high angular rates
  EXPECT_GT(corr, 0.1f);
}

TEST_F(ComOffsetCorrectionTest, AzUnchanged) {
  // CoM correction is 2D (yaw plane) — az should not be modified
  SetComOffset(0.05f, 0.03f);
  ImuData imu{};
  imu.az = 1.0f;
  calib.CorrectForComOffset(imu, 10.0f, 5.0f);
  EXPECT_FLOAT_EQ(imu.az, 1.0f);
}

TEST_F(ComOffsetCorrectionTest, GyroUnchanged) {
  // Gyro values should not be modified
  SetComOffset(0.05f, 0.03f);
  ImuData imu{};
  imu.gx = 1.0f;
  imu.gy = 2.0f;
  imu.gz = 3.0f;
  calib.CorrectForComOffset(imu, 10.0f, 5.0f);
  EXPECT_FLOAT_EQ(imu.gx, 1.0f);
  EXPECT_FLOAT_EQ(imu.gy, 2.0f);
  EXPECT_FLOAT_EQ(imu.gz, 3.0f);
}

}  // namespace
}  // namespace rc_vehicle
