#include <gtest/gtest.h>

#include <cmath>

#include "channel_controller.hpp"

namespace bench {
namespace {

constexpr float kDt = 0.002f;

ChannelController::Config MakeConfig() {
  ChannelController::Config cfg{};
  cfg.force_gains = {.kp = 4e-5f, .ki = 2e-3f, .kd = 0.0f,
                     .max_integral = 400.0f, .max_output = 1.0f};
  cfg.disp_gains = {.kp = 0.3f, .ki = 4.0f, .kd = 0.0f,
                    .max_integral = 0.2f, .max_output = 1.0f};
  cfg.output_slew_per_s = 20.0f;
  cfg.capture_ramp_s = 0.3f;
  return cfg;
}

TEST(ChannelController, StartsInDisplacementMode) {
  ChannelController c(MakeConfig());
  EXPECT_EQ(c.Mode(), ControlMode::kDisplacement);
}

TEST(ChannelController, OutputRespectsSlewLimit) {
  ChannelController c(MakeConfig());
  const float max_step = 20.0f * kDt;  // slew · dt
  ValveFeedback fb{.position_mm = 0.0f};
  float prev = c.LastOutput();
  for (int i = 0; i < 200; ++i) {
    const ValveSetpoint sp = c.Step(50.0f, fb, kDt);  // большая ошибка
    EXPECT_LE(std::fabs(sp.value - prev), max_step * 1.001f) << "tick " << i;
    prev = sp.value;
  }
}

TEST(ChannelController, RepeatedModeRequestIgnored) {
  ChannelController c(MakeConfig());
  ValveFeedback fb{.force_n = 0.0f, .position_mm = 5.0f};
  (void)c.Step(5.0f, fb, kDt);
  const float out_before = c.LastOutput();
  c.RequestMode(ControlMode::kDisplacement, 999.0f);  // текущий режим
  const ValveSetpoint sp = c.Step(5.0f, fb, kDt);
  // Захват не перезапустился: цель не прыгнула к 999.
  EXPECT_NEAR(sp.value, out_before, 20.0f * kDt * 1.001f);
  EXPECT_EQ(c.Mode(), ControlMode::kDisplacement);
}

TEST(ChannelController, BumplessSwitchToForce) {
  // Разгоняем displacement-контур до устоявшейся ненулевой команды,
  // затем переключаемся в force: скачок команды за тик не должен
  // превысить slew-лимит, а первый выход force-PID должен стартовать
  // с последней команды.
  ChannelController c(MakeConfig());
  ValveFeedback fb{.force_n = 20'000.0f, .position_mm = 4.0f};

  // Устоявшееся смещение: цель 10 мм при позиции 4 мм.
  ValveSetpoint sp{};
  for (int i = 0; i < 500; ++i) sp = c.Step(10.0f, fb, kDt);
  const float u_before = sp.value;
  ASSERT_GT(std::fabs(u_before), 0.05f);  // команда заметно ненулевая

  c.RequestMode(ControlMode::kForce, fb.force_n);
  const float max_step = 20.0f * kDt;
  float prev = u_before;
  for (int i = 0; i < 50; ++i) {
    sp = c.Step(20'000.0f, fb, kDt);  // цель = захваченная сила
    EXPECT_LE(std::fabs(sp.value - prev), max_step * 1.001f)
        << "tick " << i;
    prev = sp.value;
  }
  // При e ≈ 0 команда осталась в окрестности прежней (bumpless), а не
  // упала к нулю сброшенного регулятора.
  EXPECT_NEAR(sp.value, u_before, std::fabs(u_before) * 0.5f + 0.02f);
}

TEST(ChannelController, BumplessSwitchToDisplacementWithZeroKi) {
  // Проверка ветки ki == 0: перенос команды затухающим feed-forward.
  ChannelController::Config cfg = MakeConfig();
  cfg.disp_gains.ki = 0.0f;
  ChannelController c(cfg);
  ValveFeedback fb{.force_n = 10'000.0f, .position_mm = 3.0f};

  // Устоявшийся force-режим.
  c.RequestMode(ControlMode::kForce, fb.force_n);
  ValveSetpoint sp{};
  for (int i = 0; i < 500; ++i) sp = c.Step(30'000.0f, fb, kDt);
  const float u_before = sp.value;
  ASSERT_GT(std::fabs(u_before), 0.05f);

  c.RequestMode(ControlMode::kDisplacement, fb.position_mm);
  const float max_step = 20.0f * kDt;
  float prev = u_before;
  for (int i = 0; i < 10; ++i) {
    sp = c.Step(fb.position_mm, fb, kDt);
    EXPECT_LE(std::fabs(sp.value - prev), max_step * 1.001f);
    prev = sp.value;
  }
  // Первый тик после переключения близок к прежней команде.
  EXPECT_GT(std::fabs(sp.value), std::fabs(u_before) * 0.5f);
}

TEST(ChannelController, CaptureRampReachesExternalTarget) {
  ChannelController c(MakeConfig());
  ValveFeedback fb{.force_n = 0.0f, .position_mm = 0.0f};
  c.RequestMode(ControlMode::kForce, 0.0f);

  // 0.3 с рампы = 150 тиков; после неё эффективная цель = внешняя.
  for (int i = 0; i < 160; ++i) (void)c.Step(10'000.0f, fb, kDt);
  EXPECT_FLOAT_EQ(c.EffectiveTarget(), 10'000.0f);
}

TEST(ChannelController, DisabledOutputsEnableFalse) {
  ChannelController c(MakeConfig());
  c.SetEnabled(false);
  const ValveSetpoint sp = c.Step(0.0f, ValveFeedback{}, kDt);
  EXPECT_FALSE(sp.enable);
}

}  // namespace
}  // namespace bench
