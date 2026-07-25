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

TEST_F(KidsModeProcessorTest, SlewRateLimitsRapidThrottleIncrease) {
  cfg_.kids_mode.slew_throttle = 0.5f;  // 0.5 per second
  processor_.Init(ekf_, nullptr);

  float throttle = 0.0f;
  float steering = 0.0f;

  // First call: throttle jumps to 1.0
  throttle = 1.0f;
  processor_.Process(cfg_, throttle, steering, 100);  // 100ms = 0.1s

  // Should be limited to 0.5 * 0.1 = 0.05
  EXPECT_LE(throttle, 0.06f);  // Small tolerance
}

TEST_F(KidsModeProcessorTest, SlewRateLimitsRapidSteeringChange) {
  cfg_.kids_mode.slew_steering = 0.5f;  // 0.5 per second
  processor_.Init(ekf_, nullptr);

  float throttle = 0.0f;
  float steering = 0.0f;

  // First call: steering jumps to 0.7
  steering = 0.7f;
  processor_.Process(cfg_, throttle, steering, 100);  // 100ms = 0.1s

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
    cfg_.kids_mode.slew_throttle = 100.0f;  // отключаем slew для прямой проверки
    cfg_.kids_mode.slew_steering = 100.0f;
    cfg_.kids_mode.anti_spin_enabled = false;
    cfg_.kids_mode.accel_limit_enabled = false;
    cfg_.kids_mode.speed_limit_enabled = true;
    cfg_.kids_mode.max_speed_ms = 1.0f;
    cfg_.kids_mode.speed_limit_gain = 5.0f;

    imu_handler_ = std::make_unique<ImuHandler>(platform_, imu_calib_, madgwick_, 2);
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

TEST_F(KidsModeSpeedLimitTest, AboveLimit_ReducesThrottle) {
  ekf_.SetState(1.5f, 0.0f, 0.0f);  // 1.5 m/s > 1.0 m/s
  float throttle = 0.4f, steering = 0.0f;
  processor_.Process(cfg_, throttle, steering, 10);
  EXPECT_LT(throttle, 0.4f);
  EXPECT_TRUE(processor_.IsSpeedLimitActive());
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
  processor_.ApplySpeedLimit(cfg_, throttle);
  EXPECT_LT(throttle, 0.5f);
  EXPECT_TRUE(processor_.IsSpeedLimitActive());
}

TEST_F(KidsModeSpeedLimitTest, FarAboveLimit_ReducesThrottleButNotToZero) {
  // LOS-215: снижение — пропорциональное, но не полный обрыв в ноль.
  // speed = 3.0 m/s, max = 1.0, gain = 5 → excess=2.0,
  // reduction=min(10, kSpeedReductionMax=0.85)=0.85 → throttle=0.4*0.15=0.06
  ekf_.SetState(3.0f, 0.0f, 0.0f);
  float throttle = 0.4f, steering = 0.0f;
  processor_.Process(cfg_, throttle, steering, 10);
  EXPECT_NEAR(throttle, 0.06f, 0.005f);
  EXPECT_GT(throttle, 0.0f);
  EXPECT_TRUE(processor_.IsSpeedLimitActive());
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

TEST(KidsModeConfigTest, SpeedLimitDisabledByDefault) {
  KidsModeConfig cfg;
  EXPECT_FALSE(cfg.speed_limit_enabled);
  EXPECT_FLOAT_EQ(cfg.max_speed_ms, 1.5f);
  EXPECT_FLOAT_EQ(cfg.speed_limit_gain, 5.0f);
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
