#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "motor_config.hpp"
#include "rc_vehicle_common.hpp"

using rc_vehicle::MotorConfig;
using rc_vehicle::MotorProfile;
using rc_vehicle::ThrottlePulseUs;

// ══════════════════════════════════════════════════════════════════════════════
// MotorConfig (LOS-19): профиль мотора/ESC и выход газа
// ══════════════════════════════════════════════════════════════════════════════

TEST(MotorConfig, BrushedDefaultsMatchLegacyPwm) {
  // Без записи в NVS газ должен идти ровно как до появления профилей.
  const MotorConfig cfg = MotorConfig::Defaults(MotorProfile::BrushedPwm);
  ASSERT_TRUE(cfg.IsValid());
  for (float t : {-1.0f, -0.5f, -0.01f, 0.0f, 0.01f, 0.37f, 1.0f}) {
    EXPECT_EQ(ThrottlePulseUs(cfg, t),
              rc_vehicle::PulseWidthUsFromNormalized(t, 1000, 1500, 2000))
        << "throttle=" << t;
  }
}

TEST(MotorConfig, VescDefaultsAreValid) {
  const MotorConfig cfg = MotorConfig::Defaults(MotorProfile::VescPwm);
  EXPECT_TRUE(cfg.IsValid());
  EXPECT_EQ(cfg.profile, MotorProfile::VescPwm);
  EXPECT_EQ(ThrottlePulseUs(cfg, 0.0f), cfg.neutral_us);
}

TEST(MotorConfig, CustomRangeIsUsed) {
  MotorConfig cfg = MotorConfig::Defaults(MotorProfile::VescPwm);
  cfg.min_us = 1100;
  cfg.neutral_us = 1520;
  cfg.max_us = 1900;
  ASSERT_TRUE(cfg.IsValid());
  EXPECT_EQ(ThrottlePulseUs(cfg, -1.0f), 1100);
  EXPECT_EQ(ThrottlePulseUs(cfg, 0.0f), 1520);
  EXPECT_EQ(ThrottlePulseUs(cfg, 1.0f), 1900);
  EXPECT_EQ(ThrottlePulseUs(cfg, 0.5f), 1710);
}

TEST(MotorConfig, ClampsOutOfRangeThrottle) {
  const MotorConfig cfg = MotorConfig::Defaults(MotorProfile::BrushedPwm);
  EXPECT_EQ(ThrottlePulseUs(cfg, 5.0f), 2000);
  EXPECT_EQ(ThrottlePulseUs(cfg, -5.0f), 1000);
}

TEST(MotorConfig, NanThrottleGivesNeutral) {
  const MotorConfig cfg = MotorConfig::Defaults(MotorProfile::BrushedPwm);
  EXPECT_EQ(ThrottlePulseUs(cfg, std::numeric_limits<float>::quiet_NaN()),
            1500);
  EXPECT_EQ(ThrottlePulseUs(cfg, std::numeric_limits<float>::infinity()), 1500);
}

TEST(MotorConfig, ReversedFlipsDirection) {
  MotorConfig cfg = MotorConfig::Defaults(MotorProfile::BrushedPwm);
  cfg.reversed = true;
  EXPECT_EQ(ThrottlePulseUs(cfg, 1.0f), 1000);
  EXPECT_EQ(ThrottlePulseUs(cfg, -1.0f), 2000);
  EXPECT_EQ(ThrottlePulseUs(cfg, 0.0f), 1500);
}

TEST(MotorConfig, DeadbandHoldsNeutral) {
  MotorConfig cfg = MotorConfig::Defaults(MotorProfile::VescPwm);
  cfg.deadband = 0.1f;
  ASSERT_TRUE(cfg.IsValid());
  EXPECT_EQ(ThrottlePulseUs(cfg, 0.05f), 1500);
  EXPECT_EQ(ThrottlePulseUs(cfg, -0.1f), 1500);
}

TEST(MotorConfig, DeadbandRescalesWithoutJump) {
  MotorConfig cfg = MotorConfig::Defaults(MotorProfile::VescPwm);
  cfg.deadband = 0.2f;
  // Сразу за краем зоны — почти нейтраль, а не скачок на 20 % хода.
  EXPECT_LE(ThrottlePulseUs(cfg, 0.21f), 1507);
  EXPECT_GE(ThrottlePulseUs(cfg, -0.21f), 1493);
  // Полный ход сохраняется.
  EXPECT_EQ(ThrottlePulseUs(cfg, 1.0f), 2000);
  EXPECT_EQ(ThrottlePulseUs(cfg, -1.0f), 1000);
  // Середина оставшегося хода: (0.6 - 0.2) / 0.8 = 0.5.
  EXPECT_NEAR(ThrottlePulseUs(cfg, 0.6f), 1750, 1);
}

TEST(MotorConfig, DeadbandIsMonotonic) {
  MotorConfig cfg = MotorConfig::Defaults(MotorProfile::VescPwm);
  cfg.deadband = 0.15f;
  uint16_t prev = ThrottlePulseUs(cfg, -1.0f);
  for (int i = -99; i <= 100; ++i) {
    const uint16_t pulse = ThrottlePulseUs(cfg, static_cast<float>(i) / 100);
    EXPECT_GE(pulse, prev) << "i=" << i;
    prev = pulse;
  }
}

TEST(MotorConfig, InvalidConfigs) {
  const MotorConfig base = MotorConfig::Defaults(MotorProfile::BrushedPwm);

  MotorConfig c = base;
  c.profile = static_cast<MotorProfile>(rc_vehicle::kMotorProfileCount);
  EXPECT_FALSE(c.IsValid());

  c = base;
  c.min_us = 400;
  EXPECT_FALSE(c.IsValid());

  c = base;
  c.max_us = 2600;
  EXPECT_FALSE(c.IsValid());

  c = base;
  c.neutral_us = 1050;  // ход назад меньше kMotorMinTravelUs
  EXPECT_FALSE(c.IsValid());

  c = base;
  c.neutral_us = 2100;  // нейтраль выше максимума
  EXPECT_FALSE(c.IsValid());

  c = base;
  c.deadband = -0.01f;
  EXPECT_FALSE(c.IsValid());

  c = base;
  c.deadband = 0.6f;
  EXPECT_FALSE(c.IsValid());

  c = base;
  c.deadband = std::numeric_limits<float>::quiet_NaN();
  EXPECT_FALSE(c.IsValid());
}

TEST(MotorConfig, InvalidConfigFallsBackToBrushedDefaults) {
  MotorConfig cfg = MotorConfig::Defaults(MotorProfile::VescPwm);
  cfg.neutral_us = 900;  // невалидно: ниже min_us
  ASSERT_FALSE(cfg.IsValid());
  EXPECT_EQ(ThrottlePulseUs(cfg, 0.0f), 1500);
  EXPECT_EQ(ThrottlePulseUs(cfg, 1.0f), 2000);
}

TEST(MotorConfig, ProfileStringRoundTrip) {
  for (uint8_t i = 0; i < rc_vehicle::kMotorProfileCount; ++i) {
    const auto profile = static_cast<MotorProfile>(i);
    const auto parsed = rc_vehicle::MotorProfileFromString(
        rc_vehicle::MotorProfileToString(profile));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, profile);
  }
  EXPECT_FALSE(rc_vehicle::MotorProfileFromString("dshot").has_value());
  EXPECT_FALSE(rc_vehicle::MotorProfileFromString("").has_value());
}
