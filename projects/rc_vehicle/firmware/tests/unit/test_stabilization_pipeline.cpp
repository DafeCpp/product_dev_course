#include <gtest/gtest.h>

#include "stabilization_pipeline.hpp"

namespace rc_vehicle {
namespace {

class StabilizationPipelineTest : public ::testing::Test {
 protected:
  StabilizationPipelineTest()
      : pipeline_(yaw_, pitch_, slip_, oversteer_, kids_) {
    cfg_.enabled = true;
    cfg_.yaw_rate.pid.kp = 0.1f;
    cfg_.yaw_rate.pid.max_correction = 0.3f;
    cfg_.yaw_rate.steer_to_yaw_rate_dps = 90.0f;
    cfg_.slip_angle.pid.kp = 0.01f;
    cfg_.slip_angle.pid.max_correction = 0.3f;
    cfg_.slip_angle.target_deg = 20.0f;
    cfg_.pitch_comp.enabled = true;
    cfg_.pitch_comp.gain = 0.01f;
    cfg_.pitch_comp.max_correction = 0.25f;
    yaw_.SetGains(cfg_);
    slip_.SetGains(cfg_);

    input_.command = {.throttle = 0.2f, .steering = 0.5f};
    input_.dt_ms = 2;
    input_.speed_ms = 1.0f;
    input_.stabilization_weight = 1.0f;
    input_.mode_transition_weight = 1.0f;
    input_.imu_enabled = true;
  }

  StabilizationConfig cfg_;
  StabilizationInput input_;
  YawRateController yaw_;
  PitchCompensator pitch_;
  SlipAngleController slip_;
  OversteerGuard oversteer_;
  KidsModeProcessor kids_;
  StabilizationPipeline pipeline_;
};

TEST_F(StabilizationPipelineTest, DirectLawLeavesCommandUnchanged) {
  const ModeTraits policy{
      .yaw_rate_active = false,
      .pitch_comp_active = false,
      .slip_angle_active = false,
      .oversteer_guard_active = false,
  };

  const auto output = pipeline_.Process(cfg_, policy, input_);

  EXPECT_FLOAT_EQ(output.command.throttle, input_.command.throttle);
  EXPECT_FLOAT_EQ(output.command.steering, input_.command.steering);
  EXPECT_FLOAT_EQ(output.motor_model_target_throttle, input_.command.throttle);
}

TEST_F(StabilizationPipelineTest, NormalPolicyRunsYawController) {
  const ModeTraits policy{
      .yaw_rate_active = true,
      .pitch_comp_active = false,
      .slip_angle_active = false,
      .oversteer_guard_active = false,
  };

  const auto output = pipeline_.Process(cfg_, policy, input_);

  EXPECT_GT(output.command.steering, input_.command.steering);
  EXPECT_FLOAT_EQ(output.command.throttle, input_.command.throttle);
}

TEST_F(StabilizationPipelineTest, DriftPolicyRunsSlipController) {
  const ModeTraits policy{
      .yaw_rate_active = false,
      .pitch_comp_active = false,
      .slip_angle_active = true,
      .oversteer_guard_active = false,
  };
  input_.slip_angle_deg = 0.0f;

  const auto output = pipeline_.Process(cfg_, policy, input_);

  EXPECT_GT(output.command.throttle, input_.command.throttle);
  EXPECT_FLOAT_EQ(output.command.steering, input_.command.steering);
}

TEST_F(StabilizationPipelineTest, PitchUsesExplicitOrientationSnapshot) {
  const ModeTraits policy{
      .yaw_rate_active = false,
      .pitch_comp_active = true,
      .slip_angle_active = false,
      .oversteer_guard_active = false,
  };
  input_.pitch_deg = 10.0f;

  const auto output = pipeline_.Process(cfg_, policy, input_);

  EXPECT_NEAR(output.command.throttle, 0.3f, 1e-5f);
}

TEST_F(StabilizationPipelineTest, KidsPolicyExportsPreSpeedLimitMotorTarget) {
  cfg_.mode = DriveMode::Kids;
  cfg_.kids_mode.throttle_limit = 0.4f;
  cfg_.kids_mode.steering_limit = 0.5f;
  cfg_.kids_mode.speed_limit_enabled = false;
  input_.command = {.throttle = 0.9f, .steering = 0.9f};
  input_.dt_ms = 0;
  const ModeTraits policy{
      .yaw_rate_active = false,
      .pitch_comp_active = false,
      .slip_angle_active = false,
      .oversteer_guard_active = false,
      .apply_input_limits = true,
  };

  const auto output = pipeline_.Process(cfg_, policy, input_);

  EXPECT_FLOAT_EQ(output.command.throttle, 0.4f);
  EXPECT_FLOAT_EQ(output.command.steering, 0.5f);
  EXPECT_FLOAT_EQ(output.motor_model_target_throttle, 0.4f);
}

// ═══════════════════════════════════════════════════════════════════════════
// LOS-286: разделение семантики `enabled` и ограничителей Kids.
//
// Выключение стабилизации гасит только аддитивные контуры (через
// stabilization_weight). Ограничители Kids — отдельная подсистема защиты и
// продолжают работать; снимаются они собственным мастер-выключателем.
// ═══════════════════════════════════════════════════════════════════════════

class KidsLimitersSemanticsTest : public StabilizationPipelineTest {
 protected:
  KidsLimitersSemanticsTest() {
    cfg_.mode = DriveMode::Kids;
    cfg_.kids_mode.throttle_limit = 0.4f;
    cfg_.kids_mode.steering_limit = 0.5f;
    cfg_.kids_mode.speed_limit_enabled = false;
    input_.command = {.throttle = 0.9f, .steering = 0.9f};
    input_.dt_ms = 0;
  }

  static constexpr ModeTraits kKidsPolicy{
      .yaw_rate_active = false,
      .pitch_comp_active = false,
      .slip_angle_active = false,
      .oversteer_guard_active = false,
      .apply_input_limits = true,
  };
};

TEST_F(KidsLimitersSemanticsTest, LimitsStillApplyWhenStabilizationDisabled) {
  cfg_.enabled = false;
  input_.stabilization_weight = 0.0f;

  const auto output = pipeline_.Process(cfg_, kKidsPolicy, input_);

  EXPECT_FLOAT_EQ(output.command.throttle, 0.4f);
  EXPECT_FLOAT_EQ(output.command.steering, 0.5f);
}

TEST_F(KidsLimitersSemanticsTest, LimitsSkippedWhenMasterSwitchOff) {
  cfg_.kids_mode.limiters_enabled = false;

  const auto output = pipeline_.Process(cfg_, kKidsPolicy, input_);

  EXPECT_FLOAT_EQ(output.command.throttle, 0.9f);
  EXPECT_FLOAT_EQ(output.command.steering, 0.9f);
  EXPECT_FLOAT_EQ(output.motor_model_target_throttle, 0.9f);
}

TEST_F(KidsLimitersSemanticsTest, MasterSwitchOffWinsOverEnabledStabilization) {
  cfg_.enabled = true;
  input_.stabilization_weight = 1.0f;
  cfg_.kids_mode.limiters_enabled = false;

  const auto output = pipeline_.Process(cfg_, kKidsPolicy, input_);

  EXPECT_FLOAT_EQ(output.command.throttle, 0.9f);
  EXPECT_FLOAT_EQ(output.command.steering, 0.9f);
}

// LOS-286: oversteer-гард в Kids настраивается из kids_mode.anti_spin_*
// (KidsModeStrategy::ApplyDefaults) и дублирует anti-spin процессора, поэтому
// мастер-выключатель обязан снимать и его срезание газа — иначе обещанный
// «сырой проход» в диагностическом сценарии всё равно нарушался бы.
class KidsOversteerCutTest : public StabilizationPipelineTest {
 protected:
  KidsOversteerCutTest() {
    cfg_.mode = DriveMode::Kids;
    cfg_.kids_mode.throttle_limit = 1.0f;
    cfg_.kids_mode.steering_limit = 1.0f;
    cfg_.kids_mode.anti_spin_enabled = false;
    cfg_.kids_mode.accel_limit_enabled = false;
    cfg_.kids_mode.speed_limit_enabled = false;
    cfg_.oversteer.warn_enabled = true;
    cfg_.oversteer.slip_thresh_deg = 10.0f;
    cfg_.oversteer.rate_thresh_deg_s = 30.0f;
    cfg_.oversteer.throttle_reduction = 0.7f;

    // Условия детекции заноса: два тика, второй даёт большой slip_rate.
    input_.command = {.throttle = 0.5f, .steering = 0.0f};
    input_.dt_ms = 10;
    input_.speed_ms = 2.0f;
    input_.yaw_rate_rps = 1.0f;
  }

  // Первый тик задаёт prev_slip_deg_, второй — проверяемый.
  float RunTwoTicks(const ModeTraits& policy) {
    input_.slip_angle_deg = 0.0f;
    (void)pipeline_.Process(cfg_, policy, input_);
    input_.slip_angle_deg = 30.0f;
    return pipeline_.Process(cfg_, policy, input_).command.throttle;
  }

  static constexpr ModeTraits kKidsPolicy{
      .yaw_rate_active = false,
      .pitch_comp_active = false,
      .slip_angle_active = false,
      .oversteer_guard_active = true,
      .oversteer_reduces_throttle = true,
      .apply_input_limits = true,
  };
};

TEST_F(KidsOversteerCutTest, CutsThrottleWhenLimitersEnabled) {
  EXPECT_NEAR(RunTwoTicks(kKidsPolicy), 0.5f * (1.0f - 0.7f), 1e-5f);
}

TEST_F(KidsOversteerCutTest, DoesNotCutThrottleWhenLimitersDisabled) {
  cfg_.kids_mode.limiters_enabled = false;

  EXPECT_FLOAT_EQ(RunTwoTicks(kKidsPolicy), 0.5f);
}

TEST_F(KidsOversteerCutTest, NonKidsModeUnaffectedByLimitersFlag) {
  cfg_.mode = DriveMode::Drift;
  cfg_.kids_mode.limiters_enabled = false;
  const ModeTraits drift_policy{
      .yaw_rate_active = false,
      .pitch_comp_active = false,
      .slip_angle_active = false,
      .oversteer_guard_active = true,
      .oversteer_reduces_throttle = true,
      .apply_input_limits = false,
  };

  EXPECT_NEAR(RunTwoTicks(drift_policy), 0.5f * (1.0f - 0.7f), 1e-5f);
}

}  // namespace
}  // namespace rc_vehicle
