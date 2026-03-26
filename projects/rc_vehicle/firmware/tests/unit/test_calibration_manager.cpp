#include <gtest/gtest.h>

#include "calibration_manager.hpp"
#include "mock_platform.hpp"
#include "vehicle_ekf.hpp"

using namespace rc_vehicle;
using namespace rc_vehicle::testing;

// ═══════════════════════════════════════════════════════════════════════════
// Test fixture
// ═══════════════════════════════════════════════════════════════════════════

class CalibrationManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    mgr_ = std::make_unique<CalibrationManager>(platform_, imu_calib_,
                                                 madgwick_, &ekf_);
  }

  FakePlatform platform_;
  ImuCalibration imu_calib_;
  MadgwickFilter madgwick_;
  VehicleEkf ekf_;
  std::unique_ptr<CalibrationManager> mgr_;
};

// ═══════════════════════════════════════════════════════════════════════════
// Status
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(CalibrationManagerTest, InitialStatus_IsIdle) {
  EXPECT_STREQ(mgr_->GetStatus(), "idle");
  EXPECT_EQ(mgr_->GetStage(), 0);
}

TEST_F(CalibrationManagerTest, IsAutoForwardActive_InitiallyFalse) {
  EXPECT_FALSE(mgr_->IsAutoForwardActive());
}

// ═══════════════════════════════════════════════════════════════════════════
// StartCalibration + ProcessRequest
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(CalibrationManagerTest, StartCalibration_GyroOnly_StartsCollecting) {
  mgr_->StartCalibration(false);  // gyro only
  mgr_->ProcessRequest(0);

  EXPECT_STREQ(mgr_->GetStatus(), "collecting");
}

TEST_F(CalibrationManagerTest, StartCalibration_Full_StartsCollecting) {
  mgr_->StartCalibration(true);  // full
  mgr_->ProcessRequest(0);

  EXPECT_STREQ(mgr_->GetStatus(), "collecting");
}

TEST_F(CalibrationManagerTest, ProcessRequest_NoRequest_DoesNothing) {
  // No StartCalibration() call
  mgr_->ProcessRequest(0);
  EXPECT_STREQ(mgr_->GetStatus(), "idle");
}

// ═══════════════════════════════════════════════════════════════════════════
// ProcessCompletion
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(CalibrationManagerTest, ProcessCompletion_NoStatusChange_DoesNothing) {
  // Status stays idle → no action
  mgr_->ProcessCompletion();
  EXPECT_STREQ(mgr_->GetStatus(), "idle");
}

// ═══════════════════════════════════════════════════════════════════════════
// LoadFromNvs
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(CalibrationManagerTest, LoadFromNvs_NoData_ReturnsFalse) {
  EXPECT_FALSE(mgr_->LoadFromNvs());
}

TEST_F(CalibrationManagerTest, LoadFromNvs_WithData_ReturnsTrue) {
  ImuCalibData data{};
  data.gyro_bias[0] = 0.1f;
  data.gyro_bias[1] = 0.2f;
  data.gyro_bias[2] = 0.3f;
  platform_.SetCalibData(data);

  EXPECT_TRUE(mgr_->LoadFromNvs());
}

// ═══════════════════════════════════════════════════════════════════════════
// AutoForward
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(CalibrationManagerTest, StartAutoForward_RequiresStage1) {
  // Without stage 1 calibration done, forward calib should fail
  EXPECT_FALSE(mgr_->StartAutoForwardCalibration(0.1f));
  EXPECT_FALSE(mgr_->IsAutoForwardActive());
}

TEST_F(CalibrationManagerTest, StopAutoForward_WhenNotActive_DoesNothing) {
  EXPECT_FALSE(mgr_->IsAutoForwardActive());
  mgr_->StopAutoForward();  // Should not crash
  EXPECT_FALSE(mgr_->IsAutoForwardActive());
}

TEST_F(CalibrationManagerTest, UpdateAutoForward_WhenNotActive_ReturnsZero) {
  float throttle = mgr_->UpdateAutoForward(0.0f, 1.0f, 0.0f, 0.002f);
  EXPECT_FLOAT_EQ(throttle, 0.0f);
}

TEST_F(CalibrationManagerTest, UpdateAutoForward_ZeroDt_ReturnsZero) {
  float throttle = mgr_->UpdateAutoForward(0.0f, 1.0f, 0.0f, 0.0f);
  EXPECT_FLOAT_EQ(throttle, 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// StartAutoCalibration
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(CalibrationManagerTest, StartAutoCalibration_StartsFullCalibration) {
  mgr_->StartAutoCalibration();
  EXPECT_STREQ(mgr_->GetStatus(), "collecting");
}
