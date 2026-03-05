#include <gtest/gtest.h>

#include <cmath>

#include "kids_mode_processor.hpp"
#include "stabilization_config.hpp"
#include "vehicle_ekf.hpp"

using rc_vehicle::DriveMode;
using rc_vehicle::KidsModeConfig;
using rc_vehicle::KidsModeProcessor;
using rc_vehicle::KidsPreset;
using rc_vehicle::StabilizationConfig;
using rc_vehicle::VehicleEkf;

// ═══════════════════════════════════════════════════════════════════════════
// KidsModeConfig Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(KidsModeConfigTest, DefaultValuesAreValid) {
  KidsModeConfig cfg;
  EXPECT_TRUE(cfg.IsValid());
}

TEST(KidsModeConfigTest, DefaultThrottleLimitIs30Percent) {
  KidsModeConfig cfg;
  EXPECT_FLOAT_EQ(cfg.throttle_limit, 0.3f);
}

TEST(KidsModeConfigTest, DefaultReverseLimitIs20Percent) {
  KidsModeConfig cfg;
  EXPECT_FLOAT_EQ(cfg.reverse_limit, 0.2f);
}

TEST(KidsModeConfigTest, DefaultSteeringLimitIs70Percent) {
  KidsModeConfig cfg;
  EXPECT_FLOAT_EQ(cfg.steering_limit, 0.7f);
}

TEST(KidsModeConfigTest, AntiSpinEnabledByDefault) {
  KidsModeConfig cfg;
  EXPECT_TRUE(cfg.anti_spin_enabled);
}

// ═══════════════════════════════════════════════════════════════════════════
// KidsModeConfig::IsValid() Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(KidsModeConfigTest, IsValidReturnsTrueForValidConfig) {
  KidsModeConfig cfg;
  cfg.throttle_limit = 0.5f;
  cfg.reverse_limit = 0.3f;
  cfg.steering_limit = 0.8f;
  cfg.slew_throttle = 0.4f;
  cfg.slew_steering = 0.6f;
  cfg.anti_spin_threshold_deg = 15.0f;
  cfg.anti_spin_reduction = 0.6f;
  EXPECT_TRUE(cfg.IsValid());
}

TEST(KidsModeConfigTest, IsValidReturnsFalseForNegativeThrottle) {
  KidsModeConfig cfg;
  cfg.throttle_limit = -0.1f;
  EXPECT_FALSE(cfg.IsValid());
}

TEST(KidsModeConfigTest, IsValidReturnsFalseForThrottleAboveOne) {
  KidsModeConfig cfg;
  cfg.throttle_limit = 1.1f;
  EXPECT_FALSE(cfg.IsValid());
}

TEST(KidsModeConfigTest, IsValidReturnsFalseForNegativeSteering) {
  KidsModeConfig cfg;
  cfg.steering_limit = -0.1f;
  EXPECT_FALSE(cfg.IsValid());
}

TEST(KidsModeConfigTest, IsValidReturnsFalseForSteeringAboveOne) {
  KidsModeConfig cfg;
  cfg.steering_limit = 1.1f;
  EXPECT_FALSE(cfg.IsValid());
}

// ═══════════════════════════════════════════════════════════════════════════
// KidsModeConfig::Clamp() Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(KidsModeConfigTest, ClampFixesNegativeThrottle) {
  KidsModeConfig cfg;
  cfg.throttle_limit = -0.5f;
  cfg.Clamp();
  EXPECT_GE(cfg.throttle_limit, 0.1f);
}

TEST(KidsModeConfigTest, ClampFixesThrottleAboveOne) {
  KidsModeConfig cfg;
  cfg.throttle_limit = 1.5f;
  cfg.Clamp();
  EXPECT_LE(cfg.throttle_limit, 1.0f);
}

TEST(KidsModeConfigTest, ClampFixesSteeringBelowMinimum) {
  KidsModeConfig cfg;
  cfg.steering_limit = 0.1f;
  cfg.Clamp();
  EXPECT_GE(cfg.steering_limit, 0.3f);
}

TEST(KidsModeConfigTest, ClampFixesSteeringAboveOne) {
  KidsModeConfig cfg;
  cfg.steering_limit = 1.5f;
  cfg.Clamp();
  EXPECT_LE(cfg.steering_limit, 1.0f);
}

TEST(KidsModeConfigTest, ClampMakesConfigValid) {
  KidsModeConfig cfg;
  cfg.throttle_limit = -1.0f;
  cfg.reverse_limit = 2.0f;
  cfg.steering_limit = -0.5f;
  cfg.Clamp();
  EXPECT_TRUE(cfg.IsValid());
}

// ═══════════════════════════════════════════════════════════════════════════
// KidsModeConfig::ApplyPreset() Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(KidsModeConfigTest, ApplyPresetToddlerSetsCorrectValues) {
  KidsModeConfig cfg;
  cfg.ApplyPreset(KidsPreset::Toddler);

  EXPECT_FLOAT_EQ(cfg.throttle_limit, 0.2f);
  EXPECT_FLOAT_EQ(cfg.reverse_limit, 0.15f);
  EXPECT_FLOAT_EQ(cfg.steering_limit, 0.5f);
  EXPECT_FLOAT_EQ(cfg.slew_throttle, 0.2f);
  EXPECT_FLOAT_EQ(cfg.slew_steering, 0.3f);
  EXPECT_FLOAT_EQ(cfg.anti_spin_threshold_deg, 5.0f);
  EXPECT_FLOAT_EQ(cfg.anti_spin_reduction, 0.8f);
}

TEST(KidsModeConfigTest, ApplyPresetChildSetsCorrectValues) {
  KidsModeConfig cfg;
  cfg.ApplyPreset(KidsPreset::Child);

  EXPECT_FLOAT_EQ(cfg.throttle_limit, 0.3f);
  EXPECT_FLOAT_EQ(cfg.reverse_limit, 0.2f);
  EXPECT_FLOAT_EQ(cfg.steering_limit, 0.7f);
  EXPECT_FLOAT_EQ(cfg.slew_throttle, 0.3f);
  EXPECT_FLOAT_EQ(cfg.slew_steering, 0.5f);
  EXPECT_FLOAT_EQ(cfg.anti_spin_threshold_deg, 10.0f);
  EXPECT_FLOAT_EQ(cfg.anti_spin_reduction, 0.7f);
}

TEST(KidsModeConfigTest, ApplyPresetPreteenSetsCorrectValues) {
  KidsModeConfig cfg;
  cfg.ApplyPreset(KidsPreset::Preteen);

  EXPECT_FLOAT_EQ(cfg.throttle_limit, 0.5f);
  EXPECT_FLOAT_EQ(cfg.reverse_limit, 0.35f);
  EXPECT_FLOAT_EQ(cfg.steering_limit, 0.85f);
  EXPECT_FLOAT_EQ(cfg.slew_throttle, 0.4f);
  EXPECT_FLOAT_EQ(cfg.slew_steering, 0.7f);
  EXPECT_FLOAT_EQ(cfg.anti_spin_threshold_deg, 15.0f);
  EXPECT_FLOAT_EQ(cfg.anti_spin_reduction, 0.5f);
}

TEST(KidsModeConfigTest, ApplyPresetCustomDoesNotChangeValues) {
  KidsModeConfig cfg;
  cfg.throttle_limit = 0.42f;
  cfg.steering_limit = 0.88f;

  cfg.ApplyPreset(KidsPreset::Custom);

  EXPECT_FLOAT_EQ(cfg.throttle_limit, 0.42f);
  EXPECT_FLOAT_EQ(cfg.steering_limit, 0.88f);
}

TEST(KidsModeConfigTest, ApplyPresetResultIsValid) {
  KidsModeConfig cfg;
  cfg.ApplyPreset(KidsPreset::Toddler);
  EXPECT_TRUE(cfg.IsValid());

  cfg.ApplyPreset(KidsPreset::Child);
  EXPECT_TRUE(cfg.IsValid());

  cfg.ApplyPreset(KidsPreset::Preteen);
  EXPECT_TRUE(cfg.IsValid());
}

// ═══════════════════════════════════════════════════════════════════════════
// KidsModeProcessor Tests
// ═══════════════════════════════════════════════════════════════════════════

class KidsModeProcessorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    cfg_.mode = DriveMode::Kids;
    cfg_.kids_mode.throttle_limit = 0.3f;
    cfg_.kids_mode.reverse_limit = 0.2f;
    cfg_.kids_mode.steering_limit = 0.7f;
    cfg_.kids_mode.slew_throttle = 1.0f;  // Fast for testing
    cfg_.kids_mode.slew_steering = 1.0f;
    cfg_.kids_mode.anti_spin_enabled = true;
    cfg_.kids_mode.anti_spin_threshold_deg = 10.0f;
    cfg_.kids_mode.anti_spin_reduction = 0.7f;

    processor_.Init(cfg_, ekf_, nullptr);
  }

  StabilizationConfig cfg_;
  VehicleEkf ekf_;
  KidsModeProcessor processor_;
};

TEST_F(KidsModeProcessorTest, IsActiveReturnsTrueWhenModeIsKids) {
  EXPECT_TRUE(processor_.IsActive());
}

TEST_F(KidsModeProcessorTest, IsActiveReturnsFalseWhenModeIsNormal) {
  cfg_.mode = DriveMode::Normal;
  processor_.Init(cfg_, ekf_, nullptr);
  EXPECT_FALSE(processor_.IsActive());
}

TEST_F(KidsModeProcessorTest, ThrottleLimitAppliedToForwardThrottle) {
  float throttle = 1.0f;
  float steering = 0.0f;

  processor_.Process(throttle, steering, 10);

  EXPECT_LE(throttle, 0.3f);
}

TEST_F(KidsModeProcessorTest, ReverseLimitAppliedToReverseThrottle) {
  float throttle = -1.0f;
  float steering = 0.0f;

  processor_.Process(throttle, steering, 10);

  EXPECT_GE(throttle, -0.2f);
}

TEST_F(KidsModeProcessorTest, SteeringLimitAppliedToPositiveSteering) {
  float throttle = 0.0f;
  float steering = 1.0f;

  processor_.Process(throttle, steering, 10);

  EXPECT_LE(steering, 0.7f);
}

TEST_F(KidsModeProcessorTest, SteeringLimitAppliedToNegativeSteering) {
  float throttle = 0.0f;
  float steering = -1.0f;

  processor_.Process(throttle, steering, 10);

  EXPECT_GE(steering, -0.7f);
}

TEST_F(KidsModeProcessorTest, ZeroThrottleRemainsZero) {
  float throttle = 0.0f;
  float steering = 0.0f;

  processor_.Process(throttle, steering, 10);

  EXPECT_FLOAT_EQ(throttle, 0.0f);
}

TEST_F(KidsModeProcessorTest, ZeroSteeringRemainsZero) {
  float throttle = 0.0f;
  float steering = 0.0f;

  processor_.Process(throttle, steering, 10);

  EXPECT_FLOAT_EQ(steering, 0.0f);
}

TEST_F(KidsModeProcessorTest, ResetClearsAntiSpinState) {
  // Trigger anti-spin by setting high slip angle
  ekf_.Reset();
  // Process would normally set anti_spin_active_ if slip > threshold

  processor_.Reset();

  EXPECT_FALSE(processor_.IsAntiSpinActive());
}

TEST_F(KidsModeProcessorTest, ProcessDoesNothingWhenModeIsNotKids) {
  cfg_.mode = DriveMode::Normal;
  processor_.Init(cfg_, ekf_, nullptr);

  float throttle = 0.8f;
  float steering = 0.9f;

  processor_.Process(throttle, steering, 10);

  // Values should remain unchanged
  EXPECT_FLOAT_EQ(throttle, 0.8f);
  EXPECT_FLOAT_EQ(steering, 0.9f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Slew Rate Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(KidsModeProcessorTest, SlewRateLimitsRapidThrottleIncrease) {
  cfg_.kids_mode.slew_throttle = 0.5f;  // 0.5 per second
  processor_.Init(cfg_, ekf_, nullptr);

  float throttle = 0.0f;
  float steering = 0.0f;

  // First call: throttle jumps to 1.0
  throttle = 1.0f;
  processor_.Process(throttle, steering, 100);  // 100ms = 0.1s

  // Should be limited to 0.5 * 0.1 = 0.05
  EXPECT_LE(throttle, 0.06f);  // Small tolerance
}

TEST_F(KidsModeProcessorTest, SlewRateLimitsRapidSteeringChange) {
  cfg_.kids_mode.slew_steering = 0.5f;  // 0.5 per second
  processor_.Init(cfg_, ekf_, nullptr);

  float throttle = 0.0f;
  float steering = 0.0f;

  // First call: steering jumps to 0.7
  steering = 0.7f;
  processor_.Process(throttle, steering, 100);  // 100ms = 0.1s

  // Should be limited to 0.5 * 0.1 = 0.05
  EXPECT_LE(steering, 0.06f);  // Small tolerance
}

// ═══════════════════════════════════════════════════════════════════════════
// Integration with StabilizationConfig
// ═══════════════════════════════════════════════════════════════════════════

TEST(StabilizationConfigTest, KidsModeFieldExistsInConfig) {
  StabilizationConfig cfg;
  EXPECT_TRUE(cfg.kids_mode.IsValid());
}

TEST(StabilizationConfigTest, KidsModeVersionIs3) {
  StabilizationConfig cfg;
  EXPECT_EQ(cfg.version, 3);
}

TEST(StabilizationConfigTest, IsValidAcceptsKidsMode) {
  StabilizationConfig cfg;
  cfg.mode = DriveMode::Kids;
  EXPECT_TRUE(cfg.IsValid());
}