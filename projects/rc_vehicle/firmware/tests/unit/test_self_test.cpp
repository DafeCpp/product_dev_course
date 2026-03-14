#include <gtest/gtest.h>

#include "self_test.hpp"

using rc_vehicle::SelfTest;
using rc_vehicle::SelfTestInput;
using rc_vehicle::SelfTestItem;

namespace {

/** Создать идеальный snapshot: машина на столе, всё работает */
SelfTestInput MakeIdealInput() {
  SelfTestInput in;
  in.loop_hz = 500;
  in.imu_enabled = true;
  in.gyro_x_dps = 0.1f;
  in.gyro_y_dps = -0.2f;
  in.gyro_z_dps = 0.3f;
  in.accel_x_g = 0.01f;
  in.accel_y_g = 0.02f;
  in.accel_z_g = 0.99f;
  in.pitch_deg = 0.5f;
  in.roll_deg = -0.3f;
  in.ekf_vx = 0.001f;
  in.ekf_vy = -0.002f;
  in.failsafe_active = false;
  in.calib_valid = true;
  in.log_capacity = 5000;
  in.pwm_status = 0;
  return in;
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Все проверки PASS при идеальных условиях
// ═══════════════════════════════════════════════════════════════════════════

TEST(SelfTestTest, AllPassOnIdealInput) {
  auto results = SelfTest::Run(MakeIdealInput());
  ASSERT_EQ(results.size(), 10u);
  EXPECT_TRUE(SelfTest::AllPassed(results));
  for (const auto& r : results) {
    EXPECT_TRUE(r.passed) << "FAILED: " << r.name << " value=" << r.value;
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// Проверка каждой отдельной неисправности
// ═══════════════════════════════════════════════════════════════════════════

TEST(SelfTestTest, ControlLoopTooSlow) {
  auto in = MakeIdealInput();
  in.loop_hz = 400;
  auto results = SelfTest::Run(in);
  EXPECT_FALSE(results[0].passed);
  EXPECT_FALSE(SelfTest::AllPassed(results));
}

TEST(SelfTestTest, ControlLoopTooFast) {
  auto in = MakeIdealInput();
  in.loop_hz = 600;
  auto results = SelfTest::Run(in);
  EXPECT_FALSE(results[0].passed);
}

TEST(SelfTestTest, ImuDisabled) {
  auto in = MakeIdealInput();
  in.imu_enabled = false;
  auto results = SelfTest::Run(in);
  EXPECT_FALSE(results[1].passed);
}

TEST(SelfTestTest, GyroUnstable) {
  auto in = MakeIdealInput();
  in.gyro_z_dps = 10.0f;  // > 5 dps threshold
  auto results = SelfTest::Run(in);
  EXPECT_FALSE(results[2].passed);
}

TEST(SelfTestTest, AccelWrong) {
  auto in = MakeIdealInput();
  in.accel_z_g = 0.5f;  // |accel| ≈ 0.5 g, not ~1g
  auto results = SelfTest::Run(in);
  EXPECT_FALSE(results[3].passed);
}

TEST(SelfTestTest, MadgwickNotLevel) {
  auto in = MakeIdealInput();
  in.pitch_deg = 15.0f;  // > 5° threshold
  auto results = SelfTest::Run(in);
  EXPECT_FALSE(results[4].passed);
}

TEST(SelfTestTest, EkfDrifting) {
  auto in = MakeIdealInput();
  in.ekf_vx = 0.2f;  // > 0.05 m/s threshold
  auto results = SelfTest::Run(in);
  EXPECT_FALSE(results[5].passed);
}

TEST(SelfTestTest, FailsafeActive) {
  auto in = MakeIdealInput();
  in.failsafe_active = true;
  auto results = SelfTest::Run(in);
  EXPECT_FALSE(results[6].passed);
}

TEST(SelfTestTest, CalibInvalid) {
  auto in = MakeIdealInput();
  in.calib_valid = false;
  auto results = SelfTest::Run(in);
  EXPECT_FALSE(results[7].passed);
}

TEST(SelfTestTest, TelemetryLogNotInitialized) {
  auto in = MakeIdealInput();
  in.log_capacity = 0;
  auto results = SelfTest::Run(in);
  EXPECT_FALSE(results[8].passed);
}

TEST(SelfTestTest, PwmError) {
  auto in = MakeIdealInput();
  in.pwm_status = -1;
  auto results = SelfTest::Run(in);
  EXPECT_FALSE(results[9].passed);
}

// ═══════════════════════════════════════════════════════════════════════════
// Граничные значения
// ═══════════════════════════════════════════════════════════════════════════

TEST(SelfTestTest, LoopHzBoundary490) {
  auto in = MakeIdealInput();
  in.loop_hz = 490;
  auto results = SelfTest::Run(in);
  EXPECT_TRUE(results[0].passed);
}

TEST(SelfTestTest, LoopHzBoundary510) {
  auto in = MakeIdealInput();
  in.loop_hz = 510;
  auto results = SelfTest::Run(in);
  EXPECT_TRUE(results[0].passed);
}

TEST(SelfTestTest, LoopHzBoundary489Fails) {
  auto in = MakeIdealInput();
  in.loop_hz = 489;
  auto results = SelfTest::Run(in);
  EXPECT_FALSE(results[0].passed);
}

TEST(SelfTestTest, GyroBoundaryJustUnder5) {
  auto in = MakeIdealInput();
  in.gyro_z_dps = 4.9f;
  auto results = SelfTest::Run(in);
  EXPECT_TRUE(results[2].passed);
}

TEST(SelfTestTest, AccelBoundary09) {
  auto in = MakeIdealInput();
  in.accel_x_g = 0.0f;
  in.accel_y_g = 0.0f;
  in.accel_z_g = 0.9f;
  auto results = SelfTest::Run(in);
  EXPECT_TRUE(results[3].passed);
}

TEST(SelfTestTest, AccelBoundary11) {
  auto in = MakeIdealInput();
  in.accel_x_g = 0.0f;
  in.accel_y_g = 0.0f;
  in.accel_z_g = 1.1f;
  auto results = SelfTest::Run(in);
  EXPECT_TRUE(results[3].passed);
}

// ═══════════════════════════════════════════════════════════════════════════
// Множественные ошибки
// ═══════════════════════════════════════════════════════════════════════════

TEST(SelfTestTest, MultipleFailures) {
  auto in = MakeIdealInput();
  in.imu_enabled = false;
  in.failsafe_active = true;
  in.calib_valid = false;
  auto results = SelfTest::Run(in);
  EXPECT_FALSE(SelfTest::AllPassed(results));

  int fail_count = 0;
  for (const auto& r : results) {
    if (!r.passed) ++fail_count;
  }
  EXPECT_GE(fail_count, 3);
}

TEST(SelfTestTest, ResultCount) {
  auto results = SelfTest::Run(MakeIdealInput());
  EXPECT_EQ(results.size(), 10u);
}

TEST(SelfTestTest, ValueStringsNotEmpty) {
  auto results = SelfTest::Run(MakeIdealInput());
  for (const auto& r : results) {
    EXPECT_NE(std::strlen(r.name), 0u);
    EXPECT_NE(std::strlen(r.value), 0u) << "Empty value for: " << r.name;
  }
}
