#include <gtest/gtest.h>

#include <cmath>

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
  input_.pitch_deg = -10.0f;  // нос вверх (LOS-369)

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

TEST_F(StabilizationPipelineTest, KidsPitchCompensationKeepsThrottleCap) {
  // LOS-369: в гору компенсация добавляет газ, но предел Kids не обходит.
  cfg_.mode = DriveMode::Kids;
  cfg_.kids_mode.throttle_limit = 0.4f;
  cfg_.kids_mode.speed_limit_enabled = false;
  cfg_.kids_mode.accel_limit_enabled = false;
  input_.command = {.throttle = 0.9f, .steering = 0.0f};
  input_.pitch_deg = -10.0f;  // нос вверх
  input_.dt_ms = 0;
  const ModeTraits policy{
      .yaw_rate_active = false,
      .pitch_comp_active = true,
      .slip_angle_active = false,
      .oversteer_guard_active = false,
      .apply_input_limits = true,
  };

  EXPECT_FLOAT_EQ(pipeline_.Process(cfg_, policy, input_).command.throttle,
                  0.4f);

  // Ниже предела компенсация по-прежнему добавляет газ.
  input_.command.throttle = 0.2f;
  EXPECT_NEAR(pipeline_.Process(cfg_, policy, input_).command.throttle, 0.3f,
              1e-5f);
}

TEST_F(StabilizationPipelineTest, PitchCompensationIdleAtNeutralThrottle) {
  // LOS-369: отпущенный стик на склоне не толкает машину ни вперёд, ни назад.
  const ModeTraits policy{
      .yaw_rate_active = false,
      .pitch_comp_active = true,
      .slip_angle_active = false,
      .oversteer_guard_active = false,
  };
  input_.command = {.throttle = 0.0f, .steering = 0.0f};
  for (const float pitch : {-10.0f, 10.0f}) {
    input_.pitch_deg = pitch;
    EXPECT_FLOAT_EQ(pipeline_.Process(cfg_, policy, input_).command.throttle,
                    0.0f);
  }
}

TEST_F(StabilizationPipelineTest, PitchCompensationRampsInNearNeutral) {
  // LOS-369: стик, едва вышедший из мёртвой зоны, не получает скачком полную
  // добавку — до |throttle| = 0.2 коррекция нарастает линейно.
  const ModeTraits policy{
      .yaw_rate_active = false,
      .pitch_comp_active = true,
      .slip_angle_active = false,
      .oversteer_guard_active = false,
  };
  input_.pitch_deg = -10.0f;  // подъём: полная коррекция 0.01 * 10 = 0.1
  input_.command = {.throttle = 0.02f, .steering = 0.0f};
  EXPECT_NEAR(pipeline_.Process(cfg_, policy, input_).command.throttle, 0.03f,
              1e-5f);
  input_.command = {.throttle = 0.4f, .steering = 0.0f};
  EXPECT_NEAR(pipeline_.Process(cfg_, policy, input_).command.throttle, 0.5f,
              1e-5f);
}

TEST_F(StabilizationPipelineTest, PitchCompensationNeverFlipsThrottleSign) {
  // LOS-369: под гору газ вперёд снижается максимум до нуля, а задний ход
  // в гору не превращается в газ вперёд.
  const ModeTraits policy{
      .yaw_rate_active = false,
      .pitch_comp_active = true,
      .slip_angle_active = false,
      .oversteer_guard_active = false,
  };
  input_.pitch_deg = 30.0f;  // спуск: коррекция −0.25
  input_.command = {.throttle = 0.1f, .steering = 0.0f};
  EXPECT_FLOAT_EQ(pipeline_.Process(cfg_, policy, input_).command.throttle,
                  0.0f);
  input_.pitch_deg = -30.0f;  // подъём: коррекция +0.25
  input_.command = {.throttle = -0.1f, .steering = 0.0f};
  EXPECT_FLOAT_EQ(pipeline_.Process(cfg_, policy, input_).command.throttle,
                  0.0f);
}

TEST_F(StabilizationPipelineTest, KidsYawReverseGateUsesOriginalCommand) {
  // LOS-369: на подъёме компенсация обнуляет малый задний ход, но yaw-PID
  // всё равно должен считать, что машина едет назад, и не трогать руль.
  cfg_.mode = DriveMode::Kids;
  cfg_.kids_mode.limiters_enabled = false;
  const ModeTraits policy{
      .yaw_rate_active = true,
      .pitch_comp_active = true,
      .slip_angle_active = false,
      .oversteer_guard_active = false,
      .apply_input_limits = true,
  };
  input_.pitch_deg = -30.0f;  // подъём: коррекция +0.25
  input_.command = {.throttle = -0.1f, .steering = 0.5f};
  const auto output = pipeline_.Process(cfg_, policy, input_);
  EXPECT_FLOAT_EQ(output.command.throttle, 0.0f);
  EXPECT_FLOAT_EQ(output.command.steering, 0.5f);
}

TEST_F(StabilizationPipelineTest, KidsPitchCompensationDoesNotUndoReducers) {
  // LOS-369: accel limiter и anti-spin режут газ уже после компенсации, а
  // не до неё, — добавленный в гору газ их срез не отменяет.
  cfg_.mode = DriveMode::Kids;
  cfg_.kids_mode.throttle_limit = 0.4f;
  cfg_.kids_mode.speed_limit_enabled = false;
  cfg_.kids_mode.anti_spin_enabled = true;
  cfg_.kids_mode.anti_spin_threshold_deg = 10.0f;
  cfg_.kids_mode.anti_spin_reduction = 0.5f;
  cfg_.kids_mode.accel_limit_enabled = true;
  cfg_.kids_mode.accel_threshold_g = 0.15f;
  cfg_.kids_mode.accel_limit_gain = 3.0f;
  cfg_.kids_mode.accel_max_reduction = 0.5f;
  input_.command = {.throttle = 0.9f, .steering = 0.0f};
  input_.slip_angle_deg = 25.0f;
  input_.forward_accel_g = 0.25f;
  input_.dt_ms = 0;
  const ModeTraits policy{
      .yaw_rate_active = false,
      .pitch_comp_active = true,
      .slip_angle_active = false,
      .oversteer_guard_active = false,
      .apply_input_limits = true,
  };

  input_.pitch_deg = 0.0f;
  const float level = pipeline_.Process(cfg_, policy, input_).command.throttle;
  input_.pitch_deg = -10.0f;  // нос вверх
  const float uphill = pipeline_.Process(cfg_, policy, input_).command.throttle;

  // 0.4 × (1 − 0.5) × (1 − 0.3) на ровном и в гору одинаково.
  EXPECT_NEAR(level, 0.14f, 1e-5f);
  EXPECT_NEAR(uphill, level, 1e-5f);
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
  input_.speed_ms = 0.5f;  // накат ниже среза
  EXPECT_NEAR(RunTwoTicks(kKidsPolicy), 0.5f * (1.0f - 0.7f), 1e-5f);
}

TEST_F(KidsOversteerCutTest, CutStopsAtCoastThrottle) {
  // LOS-389: на 2 м/с при заносе 30° накат = 0.05 + 2·cos30°·0.95/8 ≈ 0.256,
  // выше среза 0.15; ниже наката противо-ЭДС тормозила бы колёса.
  const float vx = 2.0f * std::cos(30.0f * 3.14159265f / 180.0f);
  const float coast =
      cfg_.filter.motor_deadzone +
      vx * (1.0f - cfg_.filter.motor_deadzone) / cfg_.filter.motor_speed_gain;
  EXPECT_NEAR(RunTwoTicks(kKidsPolicy), coast, 1e-4f);
}

TEST_F(KidsOversteerCutTest, UntrustedSpeedGetsNoCoastFloor) {
  input_.vx_variance = 100.0f;
  EXPECT_NEAR(RunTwoTicks(kKidsPolicy), 0.5f * (1.0f - 0.7f), 1e-5f);
  input_.vx_variance = 0.0f;
  input_.ekf_diverged = true;
  EXPECT_NEAR(RunTwoTicks(kKidsPolicy), 0.5f * (1.0f - 0.7f), 1e-5f);
}

TEST_F(KidsOversteerCutTest, UnavailableMotorModelGetsNoCoastFloor) {
  cfg_.filter.motor_model_enabled = false;
  EXPECT_NEAR(RunTwoTicks(kKidsPolicy), 0.5f * (1.0f - 0.7f), 1e-5f);
  cfg_.filter.motor_model_enabled = true;
  input_.speed_calibration_active = true;
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

// ═══════════════════════════════════════════════════════════════════════════
// LOS-13: статусы лимитеров не должны переживать выход из Kids
//
// Process() чистит их сам только в ветке !IsActive, но вне Kids он не
// вызывается вовсе — сброс обязан делать пайплайн, иначе телеметрия весь
// остаток заезда приписывает срез газа лимитеру, который давно не работает.
// ═══════════════════════════════════════════════════════════════════════════

class KidsStatusLifetimeTest : public StabilizationPipelineTest {
 protected:
  KidsStatusLifetimeTest() {
    cfg_.mode = DriveMode::Kids;
    cfg_.kids_mode.throttle_limit = 0.4f;
    cfg_.kids_mode.anti_spin_enabled = true;
    cfg_.kids_mode.anti_spin_threshold_deg = 10.0f;
    cfg_.kids_mode.anti_spin_reduction = 0.7f;
    cfg_.kids_mode.accel_limit_enabled = true;
    cfg_.kids_mode.accel_threshold_g = 0.15f;
    cfg_.kids_mode.accel_limit_gain = 3.0f;
    cfg_.kids_mode.accel_max_reduction = 0.5f;
    cfg_.kids_mode.speed_limit_enabled = false;
    input_.command = {.throttle = 0.9f, .steering = 0.0f};
    input_.slip_angle_deg = 25.0f;
    input_.forward_accel_g = 0.25f;
  }

  static constexpr ModeTraits kKidsPolicy{
      .yaw_rate_active = false,
      .pitch_comp_active = false,
      .slip_angle_active = false,
      .oversteer_guard_active = false,
      .apply_input_limits = true,
  };

  static constexpr ModeTraits kNormalPolicy{
      .yaw_rate_active = false,
      .pitch_comp_active = false,
      .slip_angle_active = false,
      .oversteer_guard_active = false,
      .apply_input_limits = false,
  };
};

TEST_F(KidsStatusLifetimeTest, LeavingKidsClearsLimiterStatuses) {
  pipeline_.Process(cfg_, kKidsPolicy, input_);
  ASSERT_TRUE(kids_.IsAntiSpinActive());
  ASSERT_TRUE(kids_.IsAccelLimitActive());

  cfg_.mode = DriveMode::Normal;
  pipeline_.Process(cfg_, kNormalPolicy, input_);

  EXPECT_FALSE(kids_.IsAntiSpinActive());
  EXPECT_FALSE(kids_.IsAccelLimitActive());
  EXPECT_FALSE(kids_.IsSpeedLimitActive());
}

}  // namespace
}  // namespace rc_vehicle
