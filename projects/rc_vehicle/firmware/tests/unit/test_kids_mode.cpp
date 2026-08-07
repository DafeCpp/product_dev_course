#include <gtest/gtest.h>

#include <cmath>

#include "control_components.hpp"
#include "imu_calibration.hpp"
#include "kids_mode_processor.hpp"
#include "mock_platform.hpp"
#include "stabilization_config.hpp"
#include "vehicle_ekf.hpp"

using namespace rc_vehicle;
using namespace rc_vehicle::testing;

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

TEST(KidsModeConfigTest, DefaultSteeringSlewRateIsThreePerSecond) {
  KidsModeConfig cfg;
  EXPECT_FLOAT_EQ(cfg.slew_steering, 3.0f);
}
TEST(KidsModeConfigTest, LimitersEnabledByDefault) {
  KidsModeConfig cfg;
  EXPECT_TRUE(cfg.limiters_enabled);
}

TEST(KidsModeConfigTest, AntiSpinEnabledByDefault) {
  KidsModeConfig cfg;
  EXPECT_TRUE(cfg.anti_spin_enabled);
}

TEST(KidsModeConfigTest, AccelLimitEnabledByDefault) {
  KidsModeConfig cfg;
  EXPECT_TRUE(cfg.accel_limit_enabled);
  EXPECT_FLOAT_EQ(cfg.accel_threshold_g, 0.15f);
  EXPECT_FLOAT_EQ(cfg.accel_limit_gain, 3.0f);
  EXPECT_FLOAT_EQ(cfg.accel_max_reduction, 0.5f);
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

  EXPECT_FLOAT_EQ(cfg.throttle_limit, 0.15f);
  EXPECT_FLOAT_EQ(cfg.reverse_limit, 0.10f);
  EXPECT_FLOAT_EQ(cfg.steering_limit, 0.5f);
  EXPECT_FLOAT_EQ(cfg.slew_throttle, 0.2f);
  EXPECT_FLOAT_EQ(cfg.slew_steering, 3.0f);
  EXPECT_FLOAT_EQ(cfg.anti_spin_threshold_deg, 5.0f);
  EXPECT_FLOAT_EQ(cfg.anti_spin_reduction, 0.8f);
  EXPECT_TRUE(cfg.accel_limit_enabled);
  EXPECT_FLOAT_EQ(cfg.accel_threshold_g, 0.10f);
  EXPECT_FLOAT_EQ(cfg.accel_limit_gain, 5.0f);
  EXPECT_FLOAT_EQ(cfg.accel_max_reduction, 0.7f);
}

TEST(KidsModeConfigTest, ApplyPresetChildSetsCorrectValues) {
  KidsModeConfig cfg;
  cfg.ApplyPreset(KidsPreset::Child);

  EXPECT_FLOAT_EQ(cfg.throttle_limit, 0.3f);
  EXPECT_FLOAT_EQ(cfg.reverse_limit, 0.2f);
  EXPECT_FLOAT_EQ(cfg.steering_limit, 0.7f);
  EXPECT_FLOAT_EQ(cfg.slew_throttle, 0.3f);
  EXPECT_FLOAT_EQ(cfg.slew_steering, 3.0f);
  EXPECT_FLOAT_EQ(cfg.anti_spin_threshold_deg, 10.0f);
  EXPECT_FLOAT_EQ(cfg.anti_spin_reduction, 0.7f);
  EXPECT_TRUE(cfg.accel_limit_enabled);
  EXPECT_FLOAT_EQ(cfg.accel_threshold_g, 0.15f);
  EXPECT_FLOAT_EQ(cfg.accel_limit_gain, 3.0f);
  EXPECT_FLOAT_EQ(cfg.accel_max_reduction, 0.5f);
}

TEST(KidsModeConfigTest, ApplyPresetPreteenSetsCorrectValues) {
  KidsModeConfig cfg;
  cfg.ApplyPreset(KidsPreset::Preteen);

  EXPECT_FLOAT_EQ(cfg.throttle_limit, 0.5f);
  EXPECT_FLOAT_EQ(cfg.reverse_limit, 0.35f);
  EXPECT_FLOAT_EQ(cfg.steering_limit, 0.85f);
  EXPECT_FLOAT_EQ(cfg.slew_throttle, 0.4f);
  EXPECT_FLOAT_EQ(cfg.slew_steering, 3.0f);
  EXPECT_FLOAT_EQ(cfg.anti_spin_threshold_deg, 15.0f);
  EXPECT_FLOAT_EQ(cfg.anti_spin_reduction, 0.5f);
  EXPECT_TRUE(cfg.accel_limit_enabled);
  EXPECT_FLOAT_EQ(cfg.accel_threshold_g, 0.20f);
  EXPECT_FLOAT_EQ(cfg.accel_limit_gain, 2.0f);
  EXPECT_FLOAT_EQ(cfg.accel_max_reduction, 0.3f);
}

TEST(KidsModeConfigTest, ApplyPresetCustomDoesNotChangeValues) {
  KidsModeConfig cfg;
  cfg.throttle_limit = 0.42f;
  cfg.steering_limit = 0.88f;

  cfg.ApplyPreset(KidsPreset::Custom);

  EXPECT_FLOAT_EQ(cfg.throttle_limit, 0.42f);
  EXPECT_FLOAT_EQ(cfg.steering_limit, 0.88f);
}

// LOS-286: осознанное применение возрастного пресета возвращает защиту.
TEST(KidsModeConfigTest, ApplyPresetReenablesLimiters) {
  for (KidsPreset preset :
       {KidsPreset::Toddler, KidsPreset::Child, KidsPreset::Preteen}) {
    KidsModeConfig cfg;
    cfg.limiters_enabled = false;
    cfg.ApplyPreset(preset);
    EXPECT_TRUE(cfg.limiters_enabled) << "preset=" << static_cast<int>(preset);
  }
}

TEST(KidsModeConfigTest, ApplyPresetCustomKeepsLimitersDisabled) {
  KidsModeConfig cfg;
  cfg.limiters_enabled = false;
  cfg.ApplyPreset(KidsPreset::Custom);
  EXPECT_FALSE(cfg.limiters_enabled);
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

// ─── FW-RF4: таблица пресетов — единый источник истины ──────────────────────

// Значения, которые ApplyPreset применяет к throttle_limit/steering_limit,
// должны совпадать с таблицей, из которой строится WS-ответ kids_presets.
TEST(KidsModeConfigTest, ApplyPresetMatchesPresetTable) {
  for (const KidsPresetInfo& info : GetKidsPresetTable()) {
    if (std::isnan(info.throttle_limit)) {
      continue;  // Custom — фиксированных лимитов нет
    }
    KidsModeConfig cfg;
    cfg.ApplyPreset(info.id);
    EXPECT_FLOAT_EQ(cfg.throttle_limit, info.throttle_limit)
        << "throttle_limit mismatch for preset id "
        << static_cast<int>(info.id);
    EXPECT_FLOAT_EQ(cfg.steering_limit, info.steering_limit)
        << "steering_limit mismatch for preset id "
        << static_cast<int>(info.id);
  }
}

// Таблица содержит все четыре пресета, Custom — с NaN-лимитами.
TEST(KidsModeConfigTest, PresetTableCoversAllPresets) {
  auto table = GetKidsPresetTable();
  ASSERT_EQ(table.size(), 4u);
  EXPECT_EQ(table[0].id, KidsPreset::Custom);
  EXPECT_TRUE(std::isnan(table[0].throttle_limit));
  EXPECT_TRUE(std::isnan(table[0].steering_limit));
  EXPECT_EQ(table[1].id, KidsPreset::Toddler);
  EXPECT_EQ(table[2].id, KidsPreset::Child);
  EXPECT_EQ(table[3].id, KidsPreset::Preteen);
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

    processor_.Init(ekf_, nullptr);
  }

  StabilizationConfig cfg_;
  VehicleEkf ekf_;
  KidsModeProcessor processor_;
};

TEST_F(KidsModeProcessorTest, IsActiveReturnsTrueWhenModeIsKids) {
  EXPECT_TRUE(processor_.IsActive(cfg_));
}

TEST_F(KidsModeProcessorTest, IsActiveReturnsFalseWhenModeIsNormal) {
  cfg_.mode = DriveMode::Normal;
  processor_.Init(ekf_, nullptr);
  EXPECT_FALSE(processor_.IsActive(cfg_));
}

// LOS-286: мастер-выключатель ограничителей независим от stabilization.enabled.
TEST_F(KidsModeProcessorTest, IsActiveReturnsFalseWhenLimitersDisabled) {
  cfg_.kids_mode.limiters_enabled = false;
  EXPECT_FALSE(processor_.IsActive(cfg_));
}

TEST_F(KidsModeProcessorTest, IsActiveIgnoresStabilizationEnabledFlag) {
  cfg_.enabled = false;
  EXPECT_TRUE(processor_.IsActive(cfg_));

  cfg_.enabled = true;
  EXPECT_TRUE(processor_.IsActive(cfg_));
}

TEST_F(KidsModeProcessorTest, ProcessDoesNothingWhenLimitersDisabled) {
  cfg_.kids_mode.limiters_enabled = false;
  processor_.Init(ekf_, nullptr);

  float throttle = 0.9f;
  float steering = 0.9f;

  processor_.Process(cfg_, throttle, steering, 10);

  EXPECT_FLOAT_EQ(throttle, 0.9f);
  EXPECT_FLOAT_EQ(steering, 0.9f);
}

// Ограничители продолжают работать при выключенной стабилизации — именно эта
// семантика и вводила в заблуждение при диагностике, теперь она закреплена.
TEST_F(KidsModeProcessorTest, ProcessStillLimitsWhenStabilizationDisabled) {
  cfg_.enabled = false;
  processor_.Init(ekf_, nullptr);

  float throttle = 1.0f;
  float steering = 1.0f;

  processor_.Process(cfg_, throttle, steering, 10);

  EXPECT_FLOAT_EQ(throttle, cfg_.kids_mode.throttle_limit);
  EXPECT_FLOAT_EQ(steering, cfg_.kids_mode.steering_limit);
}

TEST_F(KidsModeProcessorTest, ThrottleLimitAppliedToForwardThrottle) {
  float throttle = 1.0f;
  float steering = 0.0f;

  processor_.Process(cfg_, throttle, steering, 10);

  EXPECT_LE(throttle, 0.3f);
}

TEST_F(KidsModeProcessorTest, ReverseLimitAppliedToReverseThrottle) {
  float throttle = -1.0f;
  float steering = 0.0f;

  processor_.Process(cfg_, throttle, steering, 10);

  EXPECT_GE(throttle, -0.2f);
}

TEST_F(KidsModeProcessorTest, SteeringLimitAppliedToPositiveSteering) {
  float throttle = 0.0f;
  float steering = 1.0f;

  processor_.Process(cfg_, throttle, steering, 10);

  EXPECT_LE(steering, 0.7f);
}

TEST_F(KidsModeProcessorTest, SteeringLimitAppliedToNegativeSteering) {
  float throttle = 0.0f;
  float steering = -1.0f;

  processor_.Process(cfg_, throttle, steering, 10);

  EXPECT_GE(steering, -0.7f);
}

TEST_F(KidsModeProcessorTest, ZeroThrottleRemainsZero) {
  float throttle = 0.0f;
  float steering = 0.0f;

  processor_.Process(cfg_, throttle, steering, 10);

  EXPECT_FLOAT_EQ(throttle, 0.0f);
}

TEST_F(KidsModeProcessorTest, ZeroSteeringRemainsZero) {
  float throttle = 0.0f;
  float steering = 0.0f;

  processor_.Process(cfg_, throttle, steering, 10);

  EXPECT_FLOAT_EQ(steering, 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Accel Limit Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(KidsModeProcessorTest, AccelLimitReducesThrottleAboveThreshold) {
  cfg_.kids_mode.accel_limit_enabled = true;
  cfg_.kids_mode.accel_threshold_g = 0.15f;
  cfg_.kids_mode.accel_limit_gain = 3.0f;
  cfg_.kids_mode.accel_max_reduction = 0.5f;
  processor_.Init(ekf_, nullptr);

  float throttle = 0.3f;
  float steering = 0.0f;

  // forward_accel = 0.25g → excess = 0.10 → reduction = 0.30
  // throttle *= (1 - 0.30) = 0.3 * 0.7 = 0.21
  processor_.Process(cfg_, throttle, steering, 10, 0.25f);

  EXPECT_LT(throttle, 0.3f);
  EXPECT_TRUE(processor_.IsAccelLimitActive());
}

TEST_F(KidsModeProcessorTest, AccelLimitNoEffectBelowThreshold) {
  cfg_.kids_mode.accel_limit_enabled = true;
  cfg_.kids_mode.accel_threshold_g = 0.15f;
  cfg_.kids_mode.slew_throttle = 100.0f;  // effectively disabled
  processor_.Init(ekf_, nullptr);

  float throttle = 0.3f;
  float steering = 0.0f;

  processor_.Process(cfg_, throttle, steering, 10, 0.10f);

  EXPECT_NEAR(throttle, 0.3f, 0.01f);
  EXPECT_FALSE(processor_.IsAccelLimitActive());
}

TEST_F(KidsModeProcessorTest, AccelLimitNoEffectForReverseThrottle) {
  cfg_.kids_mode.accel_limit_enabled = true;
  cfg_.kids_mode.accel_threshold_g = 0.15f;
  cfg_.kids_mode.slew_throttle = 100.0f;
  processor_.Init(ekf_, nullptr);

  float throttle = -0.2f;
  float steering = 0.0f;

  processor_.Process(cfg_, throttle, steering, 10, 0.30f);

  EXPECT_GE(throttle, -0.2f);
  EXPECT_FALSE(processor_.IsAccelLimitActive());
}

TEST_F(KidsModeProcessorTest, AccelLimitNoEffectWhenDisabled) {
  cfg_.kids_mode.accel_limit_enabled = false;
  cfg_.kids_mode.slew_throttle = 100.0f;
  processor_.Init(ekf_, nullptr);

  float throttle = 0.3f;
  float steering = 0.0f;

  processor_.Process(cfg_, throttle, steering, 10, 0.30f);

  EXPECT_NEAR(throttle, 0.3f, 0.01f);
  EXPECT_FALSE(processor_.IsAccelLimitActive());
}

TEST_F(KidsModeProcessorTest, AccelLimitCapsAtMaxReduction) {
  cfg_.kids_mode.accel_limit_enabled = true;
  cfg_.kids_mode.accel_threshold_g = 0.10f;
  cfg_.kids_mode.accel_limit_gain = 10.0f;
  cfg_.kids_mode.accel_max_reduction = 0.5f;
  cfg_.kids_mode.slew_throttle = 100.0f;
  processor_.Init(ekf_, nullptr);

  float throttle = 0.3f;
  float steering = 0.0f;

  // forward_accel = 0.50g → excess = 0.40 → gain*excess = 4.0 → capped at 0.5
  processor_.Process(cfg_, throttle, steering, 10, 0.50f);

  // throttle *= (1 - 0.5) = 0.3 * 0.5 = 0.15
  EXPECT_NEAR(throttle, 0.15f, 0.01f);
  EXPECT_TRUE(processor_.IsAccelLimitActive());
}

TEST_F(KidsModeProcessorTest, AccelLimitDefaultForwardAccelIsZero) {
  cfg_.kids_mode.accel_limit_enabled = true;
  cfg_.kids_mode.accel_threshold_g = 0.15f;
  cfg_.kids_mode.slew_throttle = 100.0f;
  processor_.Init(ekf_, nullptr);

  float throttle = 0.3f;
  float steering = 0.0f;

  // No forward_accel argument → default 0.0f → no reduction
  processor_.Process(cfg_, throttle, steering, 10);

  EXPECT_NEAR(throttle, 0.3f, 0.01f);
  EXPECT_FALSE(processor_.IsAccelLimitActive());
}

TEST_F(KidsModeProcessorTest, ResetClearsAccelLimitState) {
  cfg_.kids_mode.accel_limit_enabled = true;
  cfg_.kids_mode.accel_threshold_g = 0.15f;
  processor_.Init(ekf_, nullptr);

  float throttle = 0.3f;
  float steering = 0.0f;
  processor_.Process(cfg_, throttle, steering, 10, 0.30f);
  EXPECT_TRUE(processor_.IsAccelLimitActive());

  processor_.Reset();
  EXPECT_FALSE(processor_.IsAccelLimitActive());
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
  processor_.Init(ekf_, nullptr);

  float throttle = 0.8f;
  float steering = 0.9f;

  processor_.Process(cfg_, throttle, steering, 10);

  // Values should remain unchanged
  EXPECT_FLOAT_EQ(throttle, 0.8f);
  EXPECT_FLOAT_EQ(steering, 0.9f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Slew Rate Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(KidsModeProcessorTest, DoesNotApplyThrottleSlewBeforePwm) {
  cfg_.kids_mode.throttle_limit = 0.5f;
  processor_.Init(ekf_, nullptr);

  float throttle = 0.0f;
  float steering = 0.0f;

  throttle = 1.0f;
  processor_.Process(cfg_, throttle, steering, 100);  // 100ms = 0.1s

  EXPECT_FLOAT_EQ(throttle, 0.5f);
}

TEST_F(KidsModeProcessorTest, DoesNotApplySteeringSlewBeforePwm) {
  processor_.Init(ekf_, nullptr);

  float throttle = 0.0f;
  float steering = 0.0f;

  steering = 1.0f;
  processor_.Process(cfg_, throttle, steering, 100);  // 100ms = 0.1s

  EXPECT_FLOAT_EQ(steering, cfg_.kids_mode.steering_limit);
}

class KidsModeLimiterSlewTest : public ::testing::Test {
 protected:
  void SetUp() override {
    cfg_.mode = DriveMode::Kids;
    cfg_.kids_mode.throttle_limit = 0.5f;
    cfg_.kids_mode.anti_spin_enabled = true;
    cfg_.kids_mode.anti_spin_threshold_deg = 5.0f;
    cfg_.kids_mode.anti_spin_reduction = 0.5f;
    cfg_.kids_mode.accel_limit_enabled = true;
    cfg_.kids_mode.accel_threshold_g = 0.1f;
    cfg_.kids_mode.accel_limit_gain = 10.0f;
    cfg_.kids_mode.accel_max_reduction = 0.5f;

    imu_handler_ =
        std::make_unique<ImuHandler>(platform_, imu_calib_, madgwick_, 2);
    imu_handler_->SetEnabled(true);
    processor_.Init(ekf_, imu_handler_.get());
  }

  void ReachThrottleLimit() {
    float steering = 0.0f;
    for (int i = 0; i < 5; ++i) {
      float throttle = 0.5f;
      processor_.Process(cfg_, throttle, steering, 100);
    }
  }

  FakePlatform platform_;
  ImuCalibration imu_calib_;
  MadgwickFilter madgwick_;
  VehicleEkf ekf_;
  StabilizationConfig cfg_;
  KidsModeProcessor processor_;
  std::unique_ptr<ImuHandler> imu_handler_;
};

TEST_F(KidsModeLimiterSlewTest, AntiSpinAppliesBeforePwmSlew) {
  ReachThrottleLimit();
  ekf_.SetState(1.0f, 0.2f, 0.0f);  // slip angle > 5°

  float throttle = 0.5f;
  float steering = 0.0f;
  processor_.Process(cfg_, throttle, steering, 100);

  EXPECT_TRUE(processor_.IsAntiSpinActive());
  EXPECT_FLOAT_EQ(throttle, 0.25f);
}

TEST_F(KidsModeLimiterSlewTest, AccelLimitAppliesBeforePwmSlew) {
  cfg_.kids_mode.anti_spin_enabled = false;
  ReachThrottleLimit();

  float throttle = 0.5f;
  float steering = 0.0f;
  processor_.Process(cfg_, throttle, steering, 100, 0.3f);

  EXPECT_TRUE(processor_.IsAccelLimitActive());
  EXPECT_FLOAT_EQ(throttle, 0.25f);
}

TEST_F(KidsModeLimiterSlewTest, SpeedLimitAppliesBeforePwmSlew) {
  cfg_.kids_mode.anti_spin_enabled = false;
  cfg_.kids_mode.accel_limit_enabled = false;
  cfg_.kids_mode.speed_limit_enabled = true;
  cfg_.kids_mode.max_speed_ms = 1.0f;
  cfg_.kids_mode.speed_limit_gain = 1.5f;
  ReachThrottleLimit();
  ekf_.SetState(2.0f, 0.0f, 0.0f);

  float throttle = 0.5f;
  float steering = 0.0f;
  processor_.Process(cfg_, throttle, steering, 100);

  EXPECT_TRUE(processor_.IsSpeedLimitActive());
  EXPECT_FLOAT_EQ(throttle, 0.25f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Integration with StabilizationConfig
// ═══════════════════════════════════════════════════════════════════════════

TEST(StabilizationConfigTest, KidsModeFieldExistsInConfig) {
  StabilizationConfig cfg;
  EXPECT_TRUE(cfg.kids_mode.IsValid());
}

TEST(StabilizationConfigTest, ResetRestoresAllKidsModeDefaults) {
  StabilizationConfig cfg;
  cfg.kids_mode.accel_limit_enabled = false;
  cfg.kids_mode.accel_threshold_g = 0.5f;
  cfg.kids_mode.speed_limit_enabled = true;
  cfg.kids_mode.max_speed_ms = 5.0f;

  cfg.Reset();

  EXPECT_TRUE(cfg.kids_mode.accel_limit_enabled);
  EXPECT_FLOAT_EQ(cfg.kids_mode.accel_threshold_g, 0.15f);
  EXPECT_FALSE(cfg.kids_mode.speed_limit_enabled);
  EXPECT_FLOAT_EQ(cfg.kids_mode.max_speed_ms, 1.5f);
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

TEST(StabilizationConfigTest, ResetRestoresKidsLimitersFlag) {
  StabilizationConfig cfg;
  cfg.kids_mode.limiters_enabled = false;

  cfg.Reset();

  EXPECT_TRUE(cfg.kids_mode.limiters_enabled);
}

// ═══════════════════════════════════════════════════════════════════════════
// LOS-286: единый предикат ограничителей Kids. `enabled` управляет только
// контурами стабилизации и на ограничители не влияет — это и есть та
// семантика, которая раньше нигде не была зафиксирована.
// ═══════════════════════════════════════════════════════════════════════════

TEST(StabilizationConfigTest, KidsLimitersActiveRequiresKidsMode) {
  StabilizationConfig cfg;
  cfg.mode = DriveMode::Normal;
  EXPECT_FALSE(cfg.KidsLimitersActive());

  cfg.mode = DriveMode::Kids;
  EXPECT_TRUE(cfg.KidsLimitersActive());
}

TEST(StabilizationConfigTest, KidsLimitersActiveRequiresLimitersFlag) {
  StabilizationConfig cfg;
  cfg.mode = DriveMode::Kids;
  cfg.kids_mode.limiters_enabled = false;
  EXPECT_FALSE(cfg.KidsLimitersActive());
}

TEST(StabilizationConfigTest, KidsLimitersActiveIgnoresStabilizationEnabled) {
  StabilizationConfig cfg;
  cfg.mode = DriveMode::Kids;

  cfg.enabled = false;
  EXPECT_TRUE(cfg.KidsLimitersActive());

  cfg.enabled = true;
  EXPECT_TRUE(cfg.KidsLimitersActive());
}

TEST(StabilizationConfigTest, KidsSpeedLimiterActiveFollowsMasterSwitch) {
  StabilizationConfig cfg;
  cfg.mode = DriveMode::Kids;
  cfg.kids_mode.speed_limit_enabled = true;
  EXPECT_TRUE(cfg.KidsSpeedLimiterActive());

  cfg.kids_mode.limiters_enabled = false;
  EXPECT_FALSE(cfg.KidsSpeedLimiterActive());
}

// ═══════════════════════════════════════════════════════════════════════════
// FW-R21: IsActive(cfg) и лимиты читают конфиг из аргумента (живой per-tick
// снимок), процессор НЕ хранит указатель на конфиг. Так смена режима/пресета
// в рантайме сразу отражается, и нет висячего указателя на локальный конфиг.
// ═══════════════════════════════════════════════════════════════════════════

TEST(KidsModeProcessorInitTest, IsActive_TrueWhenCfgModeIsKids) {
  StabilizationConfig cfg;
  cfg.mode = DriveMode::Kids;
  VehicleEkf ekf;
  KidsModeProcessor proc;
  proc.Init(ekf, nullptr);
  EXPECT_TRUE(proc.IsActive(cfg));
}

TEST(KidsModeProcessorInitTest, IsActive_FalseWhenCfgModeIsNormal) {
  StabilizationConfig cfg;
  cfg.mode = DriveMode::Normal;
  VehicleEkf ekf;
  KidsModeProcessor proc;
  proc.Init(ekf, nullptr);
  EXPECT_FALSE(proc.IsActive(cfg));
}

TEST(KidsModeProcessorInitTest, IsActive_ReflectsCfgPassedAtCall) {
  // Один и тот же процессор отдаёт результат по переданному конфигу —
  // переключение режима снаружи отражается немедленно.
  VehicleEkf ekf;
  KidsModeProcessor proc;
  proc.Init(ekf, nullptr);

  StabilizationConfig normal;
  normal.mode = DriveMode::Normal;
  EXPECT_FALSE(proc.IsActive(normal));

  StabilizationConfig kids;
  kids.mode = DriveMode::Kids;
  EXPECT_TRUE(proc.IsActive(kids));
}

// FW-R21 регрессия: на старте режим был НЕ Kids (как в проде — NVS-снимок при
// загрузке), затем переключение в Kids в рантайме. До фикса процессор хранил
// указатель на снимок времени Init и IsActive() оставался false → лимиты не
// применялись. Теперь конфиг приходит в Process() аргументом → лимит работает.
TEST(KidsModeProcessorRuntimeSwitchTest, LimitsApplyAfterRuntimeSwitchToKids) {
  VehicleEkf ekf;
  KidsModeProcessor proc;
  proc.Init(ekf, nullptr);  // на старте режим неизвестен/не Kids

  StabilizationConfig cfg;     // дефолт: mode == Normal
  cfg.mode = DriveMode::Kids;  // рантайм-переключение в Kids
  cfg.kids_mode.throttle_limit = 0.3f;
  cfg.kids_mode.slew_throttle = 100.0f;  // отключаем slew для прямой проверки

  float throttle = 1.0f, steering = 0.0f;
  proc.Process(cfg, throttle, steering, 10);

  EXPECT_LE(throttle, 0.3f) << "throttle_limit должен примениться после "
                               "рантайм-переключения в Kids";
}

// ═══════════════════════════════════════════════════════════════════════════
// Speed Limit Tests (EKF-based)
// ═══════════════════════════════════════════════════════════════════════════

class KidsModeSpeedLimitTest : public ::testing::Test {
 protected:
  void SetUp() override {
    cfg_.mode = DriveMode::Kids;
    cfg_.kids_mode.throttle_limit = 0.5f;
    cfg_.kids_mode.reverse_limit = 0.3f;
    cfg_.kids_mode.steering_limit = 1.0f;
    cfg_.kids_mode.slew_throttle =
        100.0f;  // отключаем slew для прямой проверки
    cfg_.kids_mode.slew_steering = 100.0f;
    cfg_.kids_mode.anti_spin_enabled = false;
    cfg_.kids_mode.accel_limit_enabled = false;
    cfg_.kids_mode.speed_limit_enabled = true;
    cfg_.kids_mode.max_speed_ms = 1.0f;
    cfg_.kids_mode.speed_limit_gain = 5.0f;

    imu_handler_ =
        std::make_unique<ImuHandler>(platform_, imu_calib_, madgwick_, 2);
    imu_handler_->SetEnabled(true);

    processor_.Init(ekf_, imu_handler_.get());
  }

  FakePlatform platform_;
  ImuCalibration imu_calib_;
  MadgwickFilter madgwick_;
  VehicleEkf ekf_;
  StabilizationConfig cfg_;
  KidsModeProcessor processor_;
  std::unique_ptr<ImuHandler> imu_handler_;
};

TEST_F(KidsModeSpeedLimitTest, BelowLimit_NoReduction) {
  ekf_.SetState(0.5f, 0.0f, 0.0f);  // 0.5 m/s < 1.0 m/s
  float throttle = 0.4f, steering = 0.0f;
  processor_.Process(cfg_, throttle, steering, 10);
  EXPECT_NEAR(throttle, 0.4f, 0.01f);
  EXPECT_FALSE(processor_.IsSpeedLimitActive());
}

TEST_F(KidsModeSpeedLimitTest, LimitersDisabled_NoReductionAndFlagCleared) {
  ekf_.SetState(1.5f, 0.0f, 0.0f);  // 1.5 m/s > 1.0 m/s
  float throttle = 0.4f, steering = 0.0f;
  processor_.Process(cfg_, throttle, steering, 10);
  ASSERT_TRUE(processor_.IsSpeedLimitActive());

  cfg_.kids_mode.limiters_enabled = false;
  throttle = 0.4f;
  processor_.Process(cfg_, throttle, steering, 10);

  EXPECT_FLOAT_EQ(throttle, 0.4f);
  EXPECT_FALSE(processor_.IsSpeedLimitActive());
}

TEST_F(KidsModeSpeedLimitTest, LimitersDisabled_ApplySpeedLimitIsNoop) {
  ekf_.SetState(1.5f, 0.0f, 0.0f);
  cfg_.kids_mode.limiters_enabled = false;

  float throttle = 0.4f;
  processor_.ApplySpeedLimit(cfg_, throttle, 10);

  EXPECT_FLOAT_EQ(throttle, 0.4f);
  EXPECT_FALSE(processor_.IsSpeedLimitActive());
}

TEST_F(KidsModeSpeedLimitTest, AboveLimit_ReducesThrottle) {
  ekf_.SetState(1.5f, 0.0f, 0.0f);  // 1.5 m/s > 1.0 m/s
  float throttle = 0.4f, steering = 0.0f;
  processor_.Process(cfg_, throttle, steering, 10);
  EXPECT_LT(throttle, 0.4f);
  EXPECT_TRUE(processor_.IsSpeedLimitActive());
}

TEST_F(KidsModeSpeedLimitTest, SnapshotExcludesAppliedSpeedLimiter) {
  // Даже при обычном (неотложенном) вызове snapshot остаётся независимым от
  // feedback limiter и пригоден для моторной модели EKF (LOS-246).
  ekf_.SetState(3.0f, 0.0f, 0.0f);
  float throttle = 0.4f, steering = 0.0f, before_speed_limit = 0.0f;

  processor_.Process(cfg_, throttle, steering, 10, 0.0f, &before_speed_limit);

  EXPECT_NEAR(before_speed_limit, 0.4f, 0.01f);
  EXPECT_LT(throttle, before_speed_limit);
}

TEST_F(KidsModeSpeedLimitTest, CanDeferSpeedLimitUntilAfterOtherModifiers) {
  ekf_.SetState(1.5f, 0.0f, 0.0f);
  float throttle = 0.4f, steering = 0.0f;

  processor_.Process(cfg_, throttle, steering, 10, 0.0f, nullptr,
                     /*apply_speed_limit=*/false);
  EXPECT_NEAR(throttle, 0.4f, 0.01f);
  EXPECT_FALSE(processor_.IsSpeedLimitActive());

  // Имитирует throttle-модификатор поздней стабилизации (pitch/oversteer).
  throttle = 0.5f;
  processor_.ApplySpeedLimit(cfg_, throttle, 10);
  EXPECT_LT(throttle, 0.5f);
  EXPECT_TRUE(processor_.IsSpeedLimitActive());
}

TEST_F(KidsModeSpeedLimitTest, FarAboveLimit_ReducesThrottleButNotToZero) {
  // LOS-285: с default motor_speed_gain=8.0/deadzone=0.05 и max_speed_ms=1.0
  // детерминированный потолок мотор-модели (0.05+1.0*0.95/8=0.16875) ниже
  // страховочного пола kMaxThrottleReductionFraction=0.5 → пол оказывается
  // строже и определяет итог: throttle=0.4*(1-0.5)=0.2. Значение совпадает с
  // цифрой из старого теста (LOS-215/247) случайно — при других
  // max_speed_ms/gain пол может не участвовать вовсе (см. тесты ниже).
  ekf_.SetState(3.0f, 0.0f, 0.0f);
  float throttle = 0.4f, steering = 0.0f;
  processor_.Process(cfg_, throttle, steering, 10);
  EXPECT_NEAR(throttle, 0.2f, 0.005f);
  EXPECT_GT(throttle, 0.0f);
  EXPECT_TRUE(processor_.IsSpeedLimitActive());
}

TEST_F(KidsModeSpeedLimitTest, ModelCapTargetsConfiguredMaxSpeedNotFloor) {
  // Когда потолок мотор-модели выше страховочного пола, ограничивать должен
  // именно он — а не фиксированные 50% команды (LOS-285: старая формула
  // всегда сходилась к произвольным 50%, независимо от max_speed_ms).
  cfg_.kids_mode.max_speed_ms = 2.0f;  // потолок модели: 0.05+2*0.95/8=0.2875
  ekf_.SetState(3.0f, 0.0f, 0.0f);
  float throttle = 0.4f, steering = 0.0f;
  processor_.Process(cfg_, throttle, steering, 10);
  EXPECT_NEAR(throttle, 0.2875f, 0.005f);
  EXPECT_GT(throttle, 0.4f * 0.5f)
      << "потолок модели (0.2875) выше страховочного пола (0.2) — должен "
         "победить именно он";
}

TEST_F(KidsModeSpeedLimitTest, Monotonic_HigherStickNeverReducesOutput) {
  // LOS-285: воспроизводим параметры реального заезда (gain=1.5,
  // motor_speed_gain=8.0, max_speed_ms=1.5). При старой формуле
  // throttle*(1-min(excess*gain,0.5)) в узком окне стика (~0.23-0.26)
  // reduction рос быстрее throttle, и итоговый газ ПАДАЛ при более сильном
  // нажатии. Мелким шагом стика по всему диапазону проверяем, что новый
  // потолок (min(throttle, cap), где cap не зависит от throttle этого же
  // тика) нигде не даёт такого провала.
  cfg_.kids_mode.max_speed_ms = 1.5f;
  cfg_.kids_mode.speed_limit_gain = 1.5f;
  const float dz = cfg_.filter.motor_deadzone;
  const float gain = cfg_.filter.motor_speed_gain;

  float prev_output = -1.0f;
  for (float stick = 0.15f; stick <= 0.5f; stick += 0.005f) {
    // Свежий процессор на каждую точку: изолируем свойство "потолок этого
    // тика не зависит от текущего throttle" от медленной адаптации
    // speed_trim_ между тиками.
    KidsModeProcessor proc;
    proc.Init(ekf_, imu_handler_.get());
    const float speed = gain * std::max(stick - dz, 0.0f) / (1.0f - dz);
    ekf_.SetState(speed, 0.0f, 0.0f);

    float throttle = stick, steering = 0.0f;
    proc.Process(cfg_, throttle, steering, 10);
    EXPECT_GE(throttle, prev_output - 1e-5f)
        << "выход упал при стике=" << stick << " (был " << prev_output
        << ", стал " << throttle << ")";
    prev_output = throttle;
  }
}

TEST_F(KidsModeSpeedLimitTest, MotorModelDisabled_FallsBackToAdaptiveTrim) {
  // Когда мотор-модель выключена, детерминированный потолок недоступен
  // (нет формулы для его вычисления) — единственным ограничителем остаётся
  // адаптивный speed_trim_, набирающий значение за несколько тиков.
  cfg_.filter.motor_model_enabled = false;
  ekf_.SetState(3.0f, 0.0f, 0.0f);  // стабильно выше max_speed_ms=1.0

  float throttle = 0.4f, steering = 0.0f;
  for (int i = 0; i < 50; ++i) {
    throttle = 0.4f;
    processor_.Process(cfg_, throttle, steering, 10);
  }
  EXPECT_TRUE(processor_.IsSpeedLimitActive());
  EXPECT_LT(throttle, 0.4f)
      << "speed_trim_ должен ужесточить потолок за несколько тиков даже без "
         "мотор-модели";
}

TEST_F(KidsModeSpeedLimitTest, MotorModelDisabled_TrimRecoversAfterSpeedDrops) {
  // Код-ревью PR #323: speed_trim_ раньше ратчетил только вниз и
  // восстанавливался лишь при полном выходе из гейта (throttle<=0, EKF
  // diverged и т.п.), а не просто когда скорость упала ниже release_speed
  // (IsSpeedLimitActive()==false) — без восстановления затяжное превышение
  // навсегда прижимало бы газ к страховочному полу до конца поездки.
  cfg_.filter.motor_model_enabled = false;

  // 1. Ратчетим trim к нижнему пределу устойчивым превышением.
  ekf_.SetState(3.0f, 0.0f, 0.0f);
  float throttle = 0.4f, steering = 0.0f;
  for (int i = 0; i < 50; ++i) {
    throttle = 0.4f;
    processor_.Process(cfg_, throttle, steering, 10);
  }
  ASSERT_NEAR(throttle, 0.2f, 0.02f)
      << "sanity: trim должен был провалиться до страховочного пола";

  // 2. "Едем" заметно ниже release_speed долго, не отпуская газ и не выходя
  // из Kids/лимитеров — только это должно вернуть trim к 1.0.
  ekf_.SetState(0.3f, 0.0f, 0.0f);
  for (int i = 0; i < 60; ++i) {
    throttle = 0.4f;
    processor_.Process(cfg_, throttle, steering, 10);
  }
  EXPECT_FALSE(processor_.IsSpeedLimitActive());

  // 3. Новый заход на превышение: если trim восстановился к 1.0, первый тик
  // почти не режет газ — как при самом первом включении лимитера.
  ekf_.SetState(3.0f, 0.0f, 0.0f);
  throttle = 0.4f;
  processor_.Process(cfg_, throttle, steering, 10);
  EXPECT_NEAR(throttle, 0.4f, 0.01f)
      << "trim не восстановился — газ сразу упал к страховочному полу, как "
         "будто предыдущее превышение никогда не заканчивалось";
}

TEST_F(KidsModeSpeedLimitTest, SpeedCalibrationActive_FallsBackToAdaptiveTrim) {
  // Код-ревью PR #323: пока активна калибровка скорости
  // (AutoDriveCoordinator::StartSpeedCalib), EKF speed_ms перестаёт быть
  // заякорен на мотор-модель (vehicle_state_estimator.cpp гасит
  // motor_model_active при speed_calibration_active) — детерминированный
  // потолок модели в этот момент целится в скорость, которую EKF больше не
  // измеряет. model_available должен учитывать это и откатываться на
  // адаптивный speed_trim_, как и при выключенной мотор-модели.
  cfg_.kids_mode.max_speed_ms = 2.0f;  // потолок модели: 0.05+2*0.95/8=0.2875
  ekf_.SetState(3.0f, 0.0f, 0.0f);

  const StabilizationInput input{
      .dt_ms = 10,
      .speed_ms = ekf_.GetSpeedMs(),
      .vx_variance = ekf_.GetVxVariance(),
      .imu_enabled = true,
      .ekf_diverged = false,
      .speed_calibration_active = true,
  };

  float throttle = 0.4f;
  processor_.ApplySpeedLimit(cfg_, throttle, input);

  // На первом тике адаптивный speed_trim_ ещё не успел просесть (стартует с
  // 1.0) — газ проходит почти без среза. Если бы model_available игнорировал
  // speed_calibration_active, детерминированный потолок мгновенно срезал бы
  // throttle до 0.2875 уже на этом тике.
  EXPECT_NEAR(throttle, 0.4f, 0.01f);
}

TEST_F(KidsModeSpeedLimitTest, DivergedEkf_NoReductionEvenAboveLimit) {
  // LOS-215: первопричина найденного бага — speed limiter доверял разошедшейся
  // оценке EKF (LOS-217) и рубил throttle почти до нуля независимо от силы
  // нажатия газа. Раздуваем ковариацию Predict-циклами без измерений — так же,
  // как реально расходится оценка при накоплении ошибки интегрирования IMU.
  for (int i = 0; i < 200; ++i) {
    ekf_.Predict(0.0f, 0.0f, 0.01f);
  }
  ASSERT_GT(ekf_.GetVxVariance(), 10.0f);  // заведомо выше порога доверия

  ekf_.SetState(20.0f, 0.0f, 0.0f);  // нефизичная скорость, как в LOS-217
  float throttle = 0.4f, steering = 0.0f;
  processor_.Process(cfg_, throttle, steering, 10);
  EXPECT_NEAR(throttle, 0.4f, 0.01f);
  EXPECT_FALSE(processor_.IsSpeedLimitActive());
}

TEST_F(KidsModeSpeedLimitTest,
       DivergedByGuardClamp_NoReductionEvenWithLowVariance) {
  // Код-ревью PR #292: мотор-модельный якорь (LOS-233) в реальном control
  // loop подаёт в EKF UpdateSpeed() каждый тик со слабым, но частым шумом
  // (speed_meas_noise=4.0 по умолчанию) — это стягивает P_[0] обратно к
  // шуму измерения, даже если сама оценка x_[0] уже клемпится
  // GuardState()-физическим максимумом как заведомо испорченная
  // (IsDiverged()==true). Проверка "только дисперсия" такое пропускает —
  // воспроизводим серией confident-но-неверных pseudo-измерений.
  for (int i = 0; i < 5; ++i) {
    ekf_.UpdateSpeed(50.0f, 4.0f);  // нефизичная цель, стандартный шум
  }
  ASSERT_TRUE(ekf_.IsDiverged());
  ASSERT_LE(ekf_.GetVxVariance(), 4.0f);  // дисперсия сама по себе "здорова"

  float throttle = 0.4f, steering = 0.0f;
  processor_.Process(cfg_, throttle, steering, 10);
  EXPECT_NEAR(throttle, 0.4f, 0.01f);
  EXPECT_FALSE(processor_.IsSpeedLimitActive());
}

TEST_F(KidsModeSpeedLimitTest, ReverseThrottle_NotAffected) {
  ekf_.SetState(1.5f, 0.0f, 0.0f);  // над лимитом
  float throttle = -0.3f, steering = 0.0f;
  processor_.Process(cfg_, throttle, steering, 10);
  EXPECT_LT(throttle, 0.0f);  // отрицательный газ не обнуляется
  EXPECT_FALSE(processor_.IsSpeedLimitActive());
}

TEST_F(KidsModeSpeedLimitTest, Disabled_NoReductionEvenAboveLimit) {
  cfg_.kids_mode.speed_limit_enabled = false;
  processor_.Init(ekf_, imu_handler_.get());

  ekf_.SetState(2.0f, 0.0f, 0.0f);
  float throttle = 0.4f, steering = 0.0f;
  processor_.Process(cfg_, throttle, steering, 10);
  EXPECT_NEAR(throttle, 0.4f, 0.01f);
  EXPECT_FALSE(processor_.IsSpeedLimitActive());
}

TEST_F(KidsModeSpeedLimitTest, NullImu_NoReduction) {
  processor_.Init(ekf_, nullptr);  // нет IMU → speed limit не срабатывает

  ekf_.SetState(2.0f, 0.0f, 0.0f);
  float throttle = 0.4f, steering = 0.0f;
  processor_.Process(cfg_, throttle, steering, 10);
  EXPECT_NEAR(throttle, 0.4f, 0.01f);
  EXPECT_FALSE(processor_.IsSpeedLimitActive());
}

TEST_F(KidsModeSpeedLimitTest, Reset_ClearsSpeedLimitActive) {
  ekf_.SetState(2.0f, 0.0f, 0.0f);
  float throttle = 0.4f, steering = 0.0f;
  processor_.Process(cfg_, throttle, steering, 10);
  EXPECT_TRUE(processor_.IsSpeedLimitActive());

  processor_.Reset();
  EXPECT_FALSE(processor_.IsSpeedLimitActive());
}

TEST_F(KidsModeSpeedLimitTest, HysteresisKeepsLimiterActiveNearThreshold) {
  ekf_.SetState(1.2f, 0.0f, 0.0f);
  float throttle = 0.4f, steering = 0.0f;
  processor_.Process(cfg_, throttle, steering, 10);
  ASSERT_TRUE(processor_.IsSpeedLimitActive());

  ekf_.SetState(0.95f, 0.0f, 0.0f);
  throttle = 0.4f;
  processor_.Process(cfg_, throttle, steering, 10);
  EXPECT_TRUE(processor_.IsSpeedLimitActive());
  EXPECT_LT(throttle, 0.4f);

  ekf_.SetState(0.89f, 0.0f, 0.0f);
  throttle = 0.4f;
  processor_.Process(cfg_, throttle, steering, 10);
  EXPECT_FALSE(processor_.IsSpeedLimitActive());
  EXPECT_NEAR(throttle, 0.4f, 0.01f);
}

TEST(KidsModeConfigTest, SpeedLimitDisabledByDefault) {
  KidsModeConfig cfg;
  EXPECT_FALSE(cfg.speed_limit_enabled);
  EXPECT_FLOAT_EQ(cfg.max_speed_ms, 1.5f);
  EXPECT_FLOAT_EQ(cfg.speed_limit_gain, 1.5f);
}

TEST(KidsModeConfigTest, SpeedLimitIsValidInRange) {
  KidsModeConfig cfg;
  cfg.max_speed_ms = 1.5f;
  cfg.speed_limit_gain = 5.0f;
  EXPECT_TRUE(cfg.IsValid());
}

TEST(KidsModeConfigTest, SpeedLimitInvalidBelowMin) {
  KidsModeConfig cfg;
  cfg.max_speed_ms = 0.1f;  // < 0.3
  EXPECT_FALSE(cfg.IsValid());
}

TEST(KidsModeConfigTest, SpeedLimitInvalidAboveMax) {
  KidsModeConfig cfg;
  cfg.max_speed_ms = 6.0f;  // > 5.0
  EXPECT_FALSE(cfg.IsValid());
}

TEST(KidsModeConfigTest, ToddlerPresetEnablesSpeedLimit) {
  KidsModeConfig cfg;
  cfg.ApplyPreset(KidsPreset::Toddler);
  EXPECT_TRUE(cfg.speed_limit_enabled);
  EXPECT_FLOAT_EQ(cfg.max_speed_ms, 0.5f);
  EXPECT_TRUE(cfg.IsValid());
}

TEST(KidsModeConfigTest, ChildPresetEnablesSpeedLimit) {
  KidsModeConfig cfg;
  cfg.ApplyPreset(KidsPreset::Child);
  EXPECT_TRUE(cfg.speed_limit_enabled);
  EXPECT_FLOAT_EQ(cfg.max_speed_ms, 1.0f);
  EXPECT_TRUE(cfg.IsValid());
}

TEST(KidsModeConfigTest, PreteenPresetEnablesSpeedLimit) {
  KidsModeConfig cfg;
  cfg.ApplyPreset(KidsPreset::Preteen);
  EXPECT_TRUE(cfg.speed_limit_enabled);
  EXPECT_FLOAT_EQ(cfg.max_speed_ms, 2.0f);
  EXPECT_TRUE(cfg.IsValid());
}
