#include <gtest/gtest.h>

#include "drive_mode_registry.hpp"
#include "drive_mode_strategy.hpp"
#include "drive_modes.hpp"
#include "stabilization_config.hpp"

using namespace rc_vehicle;

// ══════════════════════════════════════════════════════════════════════════════
// Registry: lookup по DriveMode
// ══════════════════════════════════════════════════════════════════════════════

TEST(DriveModeRegistryTest, ReturnsCorrectStrategyForEachMode) {
  EXPECT_EQ(DriveModeRegistry::Get(DriveMode::Normal).GetMode(),
            DriveMode::Normal);
  EXPECT_EQ(DriveModeRegistry::Get(DriveMode::Sport).GetMode(),
            DriveMode::Sport);
  EXPECT_EQ(DriveModeRegistry::Get(DriveMode::Drift).GetMode(),
            DriveMode::Drift);
  EXPECT_EQ(DriveModeRegistry::Get(DriveMode::Kids).GetMode(),
            DriveMode::Kids);
  EXPECT_EQ(DriveModeRegistry::Get(DriveMode::DirectLaw).GetMode(),
            DriveMode::DirectLaw);
}

TEST(DriveModeRegistryTest, InvalidModeFallsBackToNormal) {
  auto invalid = static_cast<DriveMode>(255);
  EXPECT_EQ(DriveModeRegistry::Get(invalid).GetMode(), DriveMode::Normal);
}

TEST(DriveModeRegistryTest, GetNameReturnsNonNull) {
  for (uint8_t i = 0; i <= 4; ++i) {
    auto mode = static_cast<DriveMode>(i);
    EXPECT_NE(DriveModeRegistry::Get(mode).GetName(), nullptr);
    EXPECT_GT(strlen(DriveModeRegistry::Get(mode).GetName()), 0u);
  }
}

// ══════════════════════════════════════════════════════════════════════════════
// Traits: проверка корректности для каждого режима
// ══════════════════════════════════════════════════════════════════════════════

TEST(DriveModeTraitsTest, NormalMode_YawActiveSlipInactive) {
  auto traits = DriveModeRegistry::Get(DriveMode::Normal).GetTraits();
  EXPECT_TRUE(traits.yaw_rate_active);
  EXPECT_TRUE(traits.pitch_comp_active);
  EXPECT_FALSE(traits.slip_angle_active);
  EXPECT_TRUE(traits.oversteer_guard_active);
  EXPECT_TRUE(traits.oversteer_reduces_throttle);
  EXPECT_TRUE(traits.use_slew_rate);
  EXPECT_FALSE(traits.apply_input_limits);
  EXPECT_FALSE(traits.anti_spin_active);
}

TEST(DriveModeTraitsTest, SportMode_SameAsNormal) {
  auto traits = DriveModeRegistry::Get(DriveMode::Sport).GetTraits();
  EXPECT_TRUE(traits.yaw_rate_active);
  EXPECT_FALSE(traits.slip_angle_active);
  EXPECT_TRUE(traits.oversteer_reduces_throttle);
  EXPECT_TRUE(traits.use_slew_rate);
}

TEST(DriveModeTraitsTest, DriftMode_SlipActiveYawInactive_NoThrottleReduction) {
  auto traits = DriveModeRegistry::Get(DriveMode::Drift).GetTraits();
  EXPECT_FALSE(traits.yaw_rate_active);
  EXPECT_TRUE(traits.pitch_comp_active);
  EXPECT_TRUE(traits.slip_angle_active);
  EXPECT_TRUE(traits.oversteer_guard_active);
  EXPECT_FALSE(traits.oversteer_reduces_throttle);
  EXPECT_TRUE(traits.use_slew_rate);
}

TEST(DriveModeTraitsTest, KidsMode_HasInputLimitsAndAntiSpin) {
  auto traits = DriveModeRegistry::Get(DriveMode::Kids).GetTraits();
  EXPECT_TRUE(traits.yaw_rate_active);
  EXPECT_TRUE(traits.pitch_comp_active);
  EXPECT_FALSE(traits.slip_angle_active);
  EXPECT_TRUE(traits.oversteer_guard_active);
  EXPECT_TRUE(traits.oversteer_reduces_throttle);
  EXPECT_TRUE(traits.use_slew_rate);
  EXPECT_TRUE(traits.apply_input_limits);
  EXPECT_TRUE(traits.anti_spin_active);
}

TEST(DriveModeTraitsTest, DirectLaw_EverythingDisabled) {
  auto traits = DriveModeRegistry::Get(DriveMode::DirectLaw).GetTraits();
  EXPECT_FALSE(traits.yaw_rate_active);
  EXPECT_FALSE(traits.pitch_comp_active);
  EXPECT_FALSE(traits.slip_angle_active);
  EXPECT_FALSE(traits.oversteer_guard_active);
  EXPECT_FALSE(traits.oversteer_reduces_throttle);
  EXPECT_FALSE(traits.use_slew_rate);
  EXPECT_FALSE(traits.apply_input_limits);
  EXPECT_FALSE(traits.anti_spin_active);
}

// ══════════════════════════════════════════════════════════════════════════════
// ApplyDefaults: совпадение с прежним ApplyModeDefaults
// ══════════════════════════════════════════════════════════════════════════════

class ApplyDefaultsTest : public ::testing::TestWithParam<DriveMode> {};

TEST_P(ApplyDefaultsTest, StrategyDefaultsMatchLegacy) {
  DriveMode mode = GetParam();

  // Получаем defaults через стратегию
  StabilizationConfig cfg_strategy;
  cfg_strategy.Reset();
  cfg_strategy.mode = mode;
  DriveModeRegistry::Get(mode).ApplyDefaults(cfg_strategy);

  // Получаем defaults через legacy ApplyModeDefaults (который теперь
  // делегирует в ту же стратегию)
  StabilizationConfig cfg_legacy;
  cfg_legacy.Reset();
  cfg_legacy.mode = mode;
  cfg_legacy.ApplyModeDefaults();

  // Yaw rate PID
  EXPECT_FLOAT_EQ(cfg_strategy.yaw_rate.pid.kp, cfg_legacy.yaw_rate.pid.kp);
  EXPECT_FLOAT_EQ(cfg_strategy.yaw_rate.pid.ki, cfg_legacy.yaw_rate.pid.ki);
  EXPECT_FLOAT_EQ(cfg_strategy.yaw_rate.pid.kd, cfg_legacy.yaw_rate.pid.kd);
  EXPECT_FLOAT_EQ(cfg_strategy.yaw_rate.pid.max_integral,
                  cfg_legacy.yaw_rate.pid.max_integral);
  EXPECT_FLOAT_EQ(cfg_strategy.yaw_rate.pid.max_correction,
                  cfg_legacy.yaw_rate.pid.max_correction);

  // Slip angle PID
  EXPECT_FLOAT_EQ(cfg_strategy.slip_angle.pid.kp,
                  cfg_legacy.slip_angle.pid.kp);
  EXPECT_FLOAT_EQ(cfg_strategy.slip_angle.pid.kd,
                  cfg_legacy.slip_angle.pid.kd);
  EXPECT_FLOAT_EQ(cfg_strategy.slip_angle.target_deg,
                  cfg_legacy.slip_angle.target_deg);

  // Pitch compensation
  EXPECT_FLOAT_EQ(cfg_strategy.pitch_comp.gain, cfg_legacy.pitch_comp.gain);
  EXPECT_FLOAT_EQ(cfg_strategy.pitch_comp.max_correction,
                  cfg_legacy.pitch_comp.max_correction);
}

INSTANTIATE_TEST_SUITE_P(AllModes, ApplyDefaultsTest,
                         ::testing::Values(DriveMode::Normal, DriveMode::Sport,
                                           DriveMode::Drift, DriveMode::Kids,
                                           DriveMode::DirectLaw));

// ══════════════════════════════════════════════════════════════════════════════
// Normal: проверка конкретных PID-значений
// ══════════════════════════════════════════════════════════════════════════════

TEST(NormalModeDefaultsTest, HasExpectedPidValues) {
  StabilizationConfig cfg;
  cfg.Reset();
  NormalModeStrategy{}.ApplyDefaults(cfg);

  EXPECT_FLOAT_EQ(cfg.yaw_rate.pid.kp, 0.10f);
  EXPECT_FLOAT_EQ(cfg.yaw_rate.pid.ki, 0.00f);
  EXPECT_FLOAT_EQ(cfg.yaw_rate.pid.kd, 0.005f);
  EXPECT_FLOAT_EQ(cfg.yaw_rate.steer_to_yaw_rate_dps, 90.0f);
  EXPECT_FLOAT_EQ(cfg.slip_angle.pid.kp, 0.0f);
  EXPECT_FLOAT_EQ(cfg.slip_angle.pid.max_correction, 0.0f);
}

// ══════════════════════════════════════════════════════════════════════════════
// Sport: проверка конкретных PID-значений
// ══════════════════════════════════════════════════════════════════════════════

TEST(SportModeDefaultsTest, HasHigherGains) {
  StabilizationConfig cfg;
  cfg.Reset();
  SportModeStrategy{}.ApplyDefaults(cfg);

  EXPECT_FLOAT_EQ(cfg.yaw_rate.pid.kp, 0.20f);
  EXPECT_FLOAT_EQ(cfg.yaw_rate.pid.ki, 0.01f);
  EXPECT_FLOAT_EQ(cfg.yaw_rate.steer_to_yaw_rate_dps, 120.0f);
  EXPECT_FLOAT_EQ(cfg.yaw_rate.pid.max_correction, 0.40f);
}

// ══════════════════════════════════════════════════════════════════════════════
// Drift: slip angle PID включён
// ══════════════════════════════════════════════════════════════════════════════

TEST(DriftModeDefaultsTest, HasSlipAnglePid) {
  StabilizationConfig cfg;
  cfg.Reset();
  DriftModeStrategy{}.ApplyDefaults(cfg);

  EXPECT_GT(cfg.slip_angle.pid.kp, 0.0f);
  EXPECT_FLOAT_EQ(cfg.slip_angle.target_deg, 15.0f);
  EXPECT_FLOAT_EQ(cfg.slip_angle.pid.max_correction, 0.25f);
}

// ══════════════════════════════════════════════════════════════════════════════
// Kids: усиленные параметры стабилизации
// ══════════════════════════════════════════════════════════════════════════════

TEST(KidsModeDefaultsTest, EnablesPitchCompAndAdaptive) {
  StabilizationConfig cfg;
  cfg.Reset();
  KidsModeStrategy{}.ApplyDefaults(cfg);

  EXPECT_TRUE(cfg.pitch_comp.enabled);
  EXPECT_TRUE(cfg.oversteer.warn_enabled);
  EXPECT_TRUE(cfg.adaptive.enabled);
  EXPECT_FLOAT_EQ(cfg.yaw_rate.pid.kp, 0.15f);
  EXPECT_FLOAT_EQ(cfg.yaw_rate.steer_to_yaw_rate_dps, 75.0f);
}

// ══════════════════════════════════════════════════════════════════════════════
// DirectLaw: всё выключено
// ══════════════════════════════════════════════════════════════════════════════

TEST(DirectLawDefaultsTest, DisablesEverything) {
  StabilizationConfig cfg;
  cfg.Reset();
  cfg.enabled = true;  // должно быть сброшено
  DirectLawStrategy{}.ApplyDefaults(cfg);

  EXPECT_FALSE(cfg.enabled);
  EXPECT_FLOAT_EQ(cfg.yaw_rate.pid.kp, 0.0f);
  EXPECT_FLOAT_EQ(cfg.yaw_rate.pid.max_correction, 0.0f);
  EXPECT_FALSE(cfg.pitch_comp.enabled);
  EXPECT_FLOAT_EQ(cfg.slip_angle.pid.kp, 0.0f);
}

// ══════════════════════════════════════════════════════════════════════════════
// ModeTraits: POD-тип, дефолтные значения
// ══════════════════════════════════════════════════════════════════════════════

TEST(ModeTraitsTest, DefaultValues) {
  ModeTraits traits;
  EXPECT_TRUE(traits.yaw_rate_active);
  EXPECT_TRUE(traits.pitch_comp_active);
  EXPECT_FALSE(traits.slip_angle_active);
  EXPECT_TRUE(traits.oversteer_guard_active);
  EXPECT_TRUE(traits.oversteer_reduces_throttle);
  EXPECT_TRUE(traits.use_slew_rate);
  EXPECT_FALSE(traits.apply_input_limits);
  EXPECT_FALSE(traits.anti_spin_active);
}
