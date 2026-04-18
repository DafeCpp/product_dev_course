#include <gtest/gtest.h>

#include "mock_platform.hpp"
#include "stabilization_manager.hpp"

using namespace rc_vehicle;
using namespace rc_vehicle::testing;

// ═══════════════════════════════════════════════════════════════════════════
// Test fixture
// ═══════════════════════════════════════════════════════════════════════════

class StabilizationManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    mgr_ = std::make_unique<StabilizationManager>(
        platform_, madgwick_, yaw_ctrl_, slip_ctrl_, nullptr);
  }

  FakePlatform platform_;
  MadgwickFilter madgwick_;
  ImuCalibration imu_calib_;
  VehicleEkf ekf_;

  // Controllers need Init() before use, but for testing GetConfig/SetConfig
  // and weights we can use default-constructed objects
  ImuHandler* imu_handler_{nullptr};
  YawRateController yaw_ctrl_;
  SlipAngleController slip_ctrl_;

  std::unique_ptr<StabilizationManager> mgr_;
};

// ═══════════════════════════════════════════════════════════════════════════
// GetConfig / SetConfig
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(StabilizationManagerTest, DefaultConfig_IsNotEnabled) {
  auto cfg = mgr_->GetConfig();
  EXPECT_FALSE(cfg.enabled);
}

TEST_F(StabilizationManagerTest, SetConfig_ValidConfig_ReturnsTrue) {
  StabilizationConfig cfg;
  cfg.Reset();  // Fill with valid defaults
  cfg.enabled = true;

  EXPECT_TRUE(mgr_->SetConfig(cfg, false));

  auto read_back = mgr_->GetConfig();
  EXPECT_TRUE(read_back.enabled);
}

TEST_F(StabilizationManagerTest, SetConfig_SaveToNvs_PersistsConfig) {
  StabilizationConfig cfg;
  cfg.Reset();
  cfg.enabled = true;

  EXPECT_TRUE(mgr_->SetConfig(cfg, true));

  // Verify it was saved to platform
  auto saved = platform_.LoadStabilizationConfig();
  EXPECT_TRUE(saved.has_value());
  EXPECT_TRUE(saved->enabled);
}

TEST_F(StabilizationManagerTest, SetConfig_NoSaveToNvs_DoesNotPersist) {
  StabilizationConfig cfg;
  cfg.Reset();

  EXPECT_TRUE(mgr_->SetConfig(cfg, false));

  // Platform should not have the config
  auto saved = platform_.LoadStabilizationConfig();
  EXPECT_FALSE(saved.has_value());
}

// ═══════════════════════════════════════════════════════════════════════════
// LoadFromNvs
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(StabilizationManagerTest, LoadFromNvs_NoData_ReturnsFalse) {
  EXPECT_FALSE(mgr_->LoadFromNvs());
}

TEST_F(StabilizationManagerTest, LoadFromNvs_WithData_ReturnsTrue) {
  StabilizationConfig cfg;
  cfg.Reset();
  cfg.enabled = true;
  platform_.SetStabilizationConfig(cfg);

  EXPECT_TRUE(mgr_->LoadFromNvs());
  EXPECT_TRUE(mgr_->GetConfig().enabled);
}

// ═══════════════════════════════════════════════════════════════════════════
// Weights
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(StabilizationManagerTest, InitialWeights_StabZero_ModeOne) {
  EXPECT_FLOAT_EQ(mgr_->GetStabilizationWeight(), 0.0f);
  EXPECT_FLOAT_EQ(mgr_->GetModeTransitionWeight(), 1.0f);
}

TEST_F(StabilizationManagerTest, UpdateWeights_ZeroDt_NoChange) {
  mgr_->UpdateWeights(0);
  EXPECT_FLOAT_EQ(mgr_->GetStabilizationWeight(), 0.0f);
}

TEST_F(StabilizationManagerTest, UpdateWeights_EnabledWithZeroFade_ImmediateWeight) {
  StabilizationConfig cfg;
  cfg.Reset();
  cfg.enabled = true;
  cfg.fade_ms = 0;
  mgr_->SetConfig(cfg, false);

  mgr_->UpdateWeights(2);
  EXPECT_FLOAT_EQ(mgr_->GetStabilizationWeight(), 1.0f);
}

TEST_F(StabilizationManagerTest, UpdateWeights_EnabledWithFade_GradualIncrease) {
  StabilizationConfig cfg;
  cfg.Reset();
  cfg.enabled = true;
  cfg.fade_ms = 1000;  // 1 second fade
  mgr_->SetConfig(cfg, false);

  // After 100ms, weight should be ~0.1
  mgr_->UpdateWeights(100);
  float w = mgr_->GetStabilizationWeight();
  EXPECT_GT(w, 0.0f);
  EXPECT_LT(w, 0.5f);
}

TEST_F(StabilizationManagerTest, ResetWeights_SetsStabToZeroModeToOne) {
  StabilizationConfig cfg;
  cfg.Reset();
  cfg.enabled = true;
  cfg.fade_ms = 0;
  mgr_->SetConfig(cfg, false);
  mgr_->UpdateWeights(2);
  EXPECT_FLOAT_EQ(mgr_->GetStabilizationWeight(), 1.0f);

  mgr_->ResetWeights();
  EXPECT_FLOAT_EQ(mgr_->GetStabilizationWeight(), 0.0f);
  EXPECT_FLOAT_EQ(mgr_->GetModeTransitionWeight(), 1.0f);
}
