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

}  // namespace
}  // namespace rc_vehicle
