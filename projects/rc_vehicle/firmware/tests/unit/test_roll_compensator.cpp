#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "stabilization_config.hpp"
#include "stabilization_pipeline.hpp"

namespace rc_vehicle {
namespace {

// ══════════════════════════════════════════════════════════════════════════════
// RollCompensator (LOS-386)
// ══════════════════════════════════════════════════════════════════════════════

class RollCompensatorTest : public ::testing::Test {
 protected:
  RollCompensatorTest() {
    cfg_.enabled = true;
    cfg_.roll_comp.enabled = true;
    cfg_.roll_comp.gain = 0.004f;
    cfg_.roll_comp.max_correction = 0.10f;

    input_.command = {.throttle = 0.3f, .steering = 0.0f};
    input_.dt_ms = 2;
    input_.speed_ms = 2.0f;
    input_.stabilization_weight = 1.0f;
    input_.imu_enabled = true;
    input_.roll_deg = 5.0f;
  }

  float Run(float steering = 0.0f) {
    comp_.Process(cfg_, steering, input_);
    return steering;
  }

  StabilizationConfig cfg_;
  StabilizationInput input_;
  RollCompensator comp_;
};

TEST_F(RollCompensatorTest, SteersUphillOnRightSideDown) {
  // Крен > 0 — правый борт ниже, вверх по склону — влево, руль > 0.
  EXPECT_NEAR(Run(), 0.02f, 1e-6f);
  EXPECT_NEAR(comp_.GetLastCorrection(), 0.02f, 1e-6f);
}

TEST_F(RollCompensatorTest, SteersUphillOnLeftSideDown) {
  input_.roll_deg = -5.0f;
  EXPECT_NEAR(Run(), -0.02f, 1e-6f);
}

TEST_F(RollCompensatorTest, AddsToDriverSteering) {
  EXPECT_NEAR(Run(0.3f), 0.32f, 1e-6f);
}

TEST_F(RollCompensatorTest, CorrectionLimitedByMaxCorrection) {
  input_.roll_deg = 40.0f;
  EXPECT_NEAR(Run(), 0.10f, 1e-6f);
  input_.roll_deg = -40.0f;
  EXPECT_NEAR(Run(), -0.10f, 1e-6f);
}

TEST_F(RollCompensatorTest, OutputStaysWithinSteeringRange) {
  input_.roll_deg = 40.0f;
  EXPECT_FLOAT_EQ(Run(0.95f), 1.0f);
  EXPECT_NEAR(comp_.GetLastCorrection(), 0.05f, 1e-6f);
}

TEST_F(RollCompensatorTest, DisabledLeavesSteering) {
  cfg_.roll_comp.enabled = false;
  EXPECT_FLOAT_EQ(Run(0.3f), 0.3f);
  EXPECT_FLOAT_EQ(comp_.GetLastCorrection(), 0.0f);
}

TEST_F(RollCompensatorTest, IdleWithoutImu) {
  input_.imu_enabled = false;
  EXPECT_FLOAT_EQ(Run(), 0.0f);
}

TEST_F(RollCompensatorTest, IdleOnFlat) {
  input_.roll_deg = 0.0f;
  EXPECT_FLOAT_EQ(Run(0.2f), 0.2f);
}

TEST_F(RollCompensatorTest, ScaledByStabilizationWeight) {
  input_.stabilization_weight = 0.5f;
  EXPECT_NEAR(Run(), 0.01f, 1e-6f);
  input_.stabilization_weight = 0.0f;
  EXPECT_FLOAT_EQ(Run(), 0.0f);
}

TEST_F(RollCompensatorTest, RampsInWithSpeed) {
  input_.speed_ms = RollCompensator::kMinSpeedMs;
  EXPECT_FLOAT_EQ(Run(), 0.0f);
  input_.speed_ms =
      0.5f * (RollCompensator::kMinSpeedMs + RollCompensator::kFullSpeedMs);
  EXPECT_NEAR(Run(), 0.01f, 1e-6f);
  input_.speed_ms = RollCompensator::kFullSpeedMs;
  EXPECT_NEAR(Run(), 0.02f, 1e-6f);
}

TEST_F(RollCompensatorTest, IdleInReverse) {
  input_.command.throttle = -0.3f;
  EXPECT_FLOAT_EQ(Run(), 0.0f);
  input_.command.throttle = 0.0f;
  input_.speed_ms = -1.0f;
  EXPECT_FLOAT_EQ(Run(), 0.0f);
}

TEST_F(RollCompensatorTest, FadesOutInTurns) {
  // Крен корпуса в повороте не должен закручивать поворот: вес падает по
  // боковому ускорению |v·r| между kFullLatG и kZeroLatG.
  constexpr float kG = 9.80665f;
  auto set_lat_g = [&](float lat_g) {
    input_.yaw_rate_rps = lat_g * kG / input_.speed_ms;
  };
  set_lat_g(RollCompensator::kFullLatG);
  EXPECT_NEAR(Run(), 0.02f, 1e-5f);
  set_lat_g(0.5f * (RollCompensator::kFullLatG + RollCompensator::kZeroLatG));
  EXPECT_NEAR(Run(), 0.01f, 1e-5f);
  set_lat_g(RollCompensator::kZeroLatG);
  EXPECT_NEAR(Run(), 0.0f, 1e-6f);
  input_.yaw_rate_rps = -input_.yaw_rate_rps;  // поворот в другую сторону
  EXPECT_NEAR(Run(), 0.0f, 1e-6f);
}

// ══════════════════════════════════════════════════════════════════════════════
// RollCompensationConfig
// ══════════════════════════════════════════════════════════════════════════════

TEST(RollCompensationConfigTest, DefaultsAreValidAndDisabled) {
  StabilizationConfig cfg;
  EXPECT_FALSE(cfg.roll_comp.enabled);
  EXPECT_FLOAT_EQ(cfg.roll_comp.gain, 0.004f);
  EXPECT_FLOAT_EQ(cfg.roll_comp.max_correction, 0.10f);
  EXPECT_TRUE(cfg.IsValid());
}

TEST(RollCompensationConfigTest, OutOfRangeIsInvalidAndClamped) {
  StabilizationConfig cfg;
  cfg.roll_comp.gain = 0.2f;
  EXPECT_FALSE(cfg.IsValid());
  cfg.roll_comp.gain = 0.004f;
  cfg.roll_comp.max_correction = -0.1f;
  EXPECT_FALSE(cfg.IsValid());

  cfg.roll_comp.gain = 0.2f;
  cfg.roll_comp.max_correction = 0.9f;
  cfg.Clamp();
  EXPECT_FLOAT_EQ(cfg.roll_comp.gain, 0.05f);
  EXPECT_FLOAT_EQ(cfg.roll_comp.max_correction, 0.3f);
  EXPECT_TRUE(cfg.IsValid());
}

TEST(RollCompensationConfigTest, ResetRestoresDefaults) {
  StabilizationConfig cfg;
  cfg.roll_comp = {.enabled = true, .gain = 0.02f, .max_correction = 0.2f};
  cfg.Reset();
  EXPECT_FALSE(cfg.roll_comp.enabled);
  EXPECT_FLOAT_EQ(cfg.roll_comp.gain, 0.004f);
  EXPECT_FLOAT_EQ(cfg.roll_comp.max_correction, 0.10f);
}

// ══════════════════════════════════════════════════════════════════════════════
// Миграция раскладки NVS v14 → v15
// ══════════════════════════════════════════════════════════════════════════════

/// Байты StabilizationConfig в раскладке до LOS-386: всё, кроме roll_comp.
std::array<uint8_t, kStabilizationConfigPreRollCompSize> PreRollCompBytes(
    const StabilizationConfig& cfg) {
  std::array<uint8_t, kStabilizationConfigPreRollCompSize> bytes{};
  const auto* src = reinterpret_cast<const uint8_t*>(&cfg);
  constexpr size_t kPrefix = offsetof(StabilizationConfig, roll_comp);
  std::memcpy(bytes.data(), src, kPrefix);
  std::memcpy(
      bytes.data() + kPrefix, src + offsetof(StabilizationConfig, version),
      sizeof(StabilizationConfig) - offsetof(StabilizationConfig, version));
  return bytes;
}

TEST(StabilizationConfigMigrationTest, PreRollCompLayoutKeepsAllFields) {
  StabilizationConfig old_cfg;
  old_cfg.enabled = true;
  old_cfg.mode = DriveMode::Drift;
  old_cfg.yaw_rate.pid.kp = 0.123f;
  old_cfg.pitch_comp.enabled = true;
  old_cfg.pitch_comp.gain = 0.02f;
  old_cfg.kids_mode.throttle_limit = 0.35f;
  old_cfg.brake_slew_multiplier = 6.0f;
  old_cfg.roll_comp = {.enabled = true, .gain = 0.03f, .max_correction = 0.2f};
  const auto bytes = PreRollCompBytes(old_cfg);

  StabilizationConfig cfg;
  ASSERT_TRUE(
      StabilizationConfigFromPreRollComp(bytes.data(), bytes.size(), cfg));

  EXPECT_TRUE(cfg.enabled);
  EXPECT_EQ(cfg.mode, DriveMode::Drift);
  EXPECT_FLOAT_EQ(cfg.yaw_rate.pid.kp, 0.123f);
  EXPECT_TRUE(cfg.pitch_comp.enabled);
  EXPECT_FLOAT_EQ(cfg.pitch_comp.gain, 0.02f);
  EXPECT_FLOAT_EQ(cfg.kids_mode.throttle_limit, 0.35f);
  EXPECT_FLOAT_EQ(cfg.brake_slew_multiplier, 6.0f);
  EXPECT_EQ(cfg.version, old_cfg.version);
  EXPECT_EQ(cfg.magic, kStabilizationConfigMagic);
  // В старой раскладке roll_comp не было — значения по умолчанию.
  EXPECT_FALSE(cfg.roll_comp.enabled);
  EXPECT_FLOAT_EQ(cfg.roll_comp.gain, 0.004f);
  EXPECT_FLOAT_EQ(cfg.roll_comp.max_correction, 0.10f);
  EXPECT_TRUE(cfg.IsValid());
}

TEST(StabilizationConfigMigrationTest, PreRollCompRejectsOtherSizes) {
  const StabilizationConfig src;
  StabilizationConfig cfg;
  cfg.brake_slew_multiplier = 7.0f;
  EXPECT_FALSE(StabilizationConfigFromPreRollComp(
      &src, sizeof(StabilizationConfig), cfg));
  EXPECT_FALSE(StabilizationConfigFromPreRollComp(nullptr, 0, cfg));
  EXPECT_FLOAT_EQ(cfg.brake_slew_multiplier, 7.0f);
}

}  // namespace
}  // namespace rc_vehicle
