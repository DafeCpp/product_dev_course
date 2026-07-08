#include <gtest/gtest.h>

#include <cmath>

#include "control_components.hpp"
#include "imu_calibration.hpp"
#include "madgwick_filter.hpp"
#include "mock_platform.hpp"
#include "stabilization_config.hpp"
#include "stabilization_pipeline.hpp"

using namespace rc_vehicle;
using namespace rc_vehicle::testing;

// ══════════════════════════════════════════════════════════════════════════════
// Fixture: настраивает PitchCompensator с pitch_comp.enabled=true
// ══════════════════════════════════════════════════════════════════════════════

class PitchCompensatorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    imu_handler_.SetEnabled(true);

    cfg_.enabled = true;
    cfg_.pitch_comp.enabled = true;
    cfg_.pitch_comp.gain = 0.01f;          // 1% throttle per degree
    cfg_.pitch_comp.max_correction = 0.25f;

    comp_.Init(madgwick_, &imu_handler_);
  }

  /// Feed IMU data N times and update both ImuHandler and Madgwick
  void FeedImuData(float ax, float ay, float az, float gx, float gy, float gz,
                   int n = 200) {
    ImuData data{};
    data.ax = ax;
    data.ay = ay;
    data.az = az;
    data.gx = gx;
    data.gy = gy;
    data.gz = gz;
    platform_.SetImuData(data);
    for (int i = 0; i < n; ++i) {
      platform_.AdvanceTimeMs(2);
      imu_handler_.Update(platform_.GetTimeMs(), 2);
    }
  }

  /// Feed flat IMU data (no pitch, no roll)
  void FeedFlat(int n = 200) { FeedImuData(0.0f, 0.0f, 1.0f, 0, 0, 0, n); }

  /// Feed tilted IMU data: nose up by pitch_deg degrees
  /// accel_x = -sin(pitch) * g, accel_z = cos(pitch) * g
  void FeedPitchUp(float pitch_deg, int n = 300) {
    const float rad = pitch_deg * static_cast<float>(M_PI) / 180.0f;
    FeedImuData(-std::sin(rad), 0.0f, std::cos(rad), 0, 0, 0, n);
  }

  /// Feed tilted IMU data: nose down
  void FeedPitchDown(float pitch_deg, int n = 300) {
    FeedPitchUp(-pitch_deg, n);
  }

  FakePlatform platform_;
  ImuCalibration calib_;
  MadgwickFilter madgwick_;
  ImuHandler imu_handler_{platform_, calib_, madgwick_};

  StabilizationConfig cfg_;
  PitchCompensator comp_;
};

// ══════════════════════════════════════════════════════════════════════════════
// Tests
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(PitchCompensatorTest, NoCorrection_WhenFlat) {
  FeedFlat();
  float throttle = 0.5f;
  comp_.Process(cfg_, throttle, 1.0f);
  EXPECT_NEAR(throttle, 0.5f, 0.02f)
      << "No pitch correction on flat surface";
}

TEST_F(PitchCompensatorTest, IncreasesThrottle_WhenNoseUp) {
  FeedPitchUp(20.0f);
  float throttle = 0.5f;
  comp_.Process(cfg_, throttle, 1.0f);
  EXPECT_GT(throttle, 0.5f)
      << "Nose up (positive pitch) should increase throttle";
}

TEST_F(PitchCompensatorTest, DecreasesThrottle_WhenNoseDown) {
  FeedPitchDown(20.0f);
  float throttle = 0.5f;
  comp_.Process(cfg_, throttle, 1.0f);
  EXPECT_LT(throttle, 0.5f)
      << "Nose down (negative pitch) should decrease throttle";
}

// FW-R23: процессор НЕ хранит указатель на конфиг — читает переданный per-tick.
// Один и тот же (про-)инициализированный компенсатор учитывает РАЗНЫЕ объекты
// конфига, переданные в Process(). До фикса cfg_ указывал на локал из
// unified_init (висячий указатель / boot-снимок), и рантайм-изменения не
// доезжали.
TEST_F(PitchCompensatorTest, UsesConfigPassedPerCall_NotStored) {
  FeedPitchUp(20.0f);

  StabilizationConfig off = cfg_;
  off.pitch_comp.enabled = false;
  float throttle_off = 0.5f;
  comp_.Process(off, throttle_off, 1.0f);
  EXPECT_FLOAT_EQ(throttle_off, 0.5f)
      << "disabled в переданном cfg → нет коррекции";

  StabilizationConfig on = cfg_;
  on.pitch_comp.enabled = true;
  float throttle_on = 0.5f;
  comp_.Process(on, throttle_on, 1.0f);
  EXPECT_GT(throttle_on, 0.5f) << "enabled в переданном cfg → коррекция есть";
}

TEST_F(PitchCompensatorTest, CorrectionClamped_ByMaxCorrection) {
  // With gain=0.01 and max_correction=0.25, 30° pitch → correction = 0.30
  // Should be clamped to 0.25
  FeedPitchUp(30.0f, 500);

  float pitch_deg = 0, roll_deg = 0, yaw_deg = 0;
  madgwick_.GetEulerDeg(pitch_deg, roll_deg, yaw_deg);

  float throttle = 0.5f;
  comp_.Process(cfg_, throttle, 1.0f);
  float correction = throttle - 0.5f;
  EXPECT_LE(correction, cfg_.pitch_comp.max_correction + 0.01f)
      << "Correction should be clamped by max_correction";
}

TEST_F(PitchCompensatorTest, NoEffect_WhenDisabled) {
  cfg_.pitch_comp.enabled = false;
  comp_.Init(madgwick_, &imu_handler_);
  FeedPitchUp(20.0f);
  float throttle = 0.5f;
  comp_.Process(cfg_, throttle, 1.0f);
  EXPECT_FLOAT_EQ(throttle, 0.5f) << "No correction when pitch_comp disabled";
}

TEST_F(PitchCompensatorTest, NoEffect_WhenStabWeightZero) {
  FeedPitchUp(20.0f);
  float throttle = 0.5f;
  comp_.Process(cfg_, throttle, 0.0f);
  EXPECT_FLOAT_EQ(throttle, 0.5f) << "stab_w=0 → no correction";
}

TEST_F(PitchCompensatorTest, NoEffect_WhenImuDisabled) {
  FeedPitchUp(20.0f);
  imu_handler_.SetEnabled(false);
  float throttle = 0.5f;
  comp_.Process(cfg_, throttle, 1.0f);
  EXPECT_FLOAT_EQ(throttle, 0.5f) << "No correction when IMU disabled";
}

TEST_F(PitchCompensatorTest, CorrectionScaledByStabWeight) {
  FeedPitchUp(15.0f);

  float throttle1 = 0.5f;
  comp_.Process(cfg_, throttle1, 1.0f);
  float correction_full = throttle1 - 0.5f;

  float throttle2 = 0.5f;
  comp_.Process(cfg_, throttle2, 0.5f);
  float correction_half = throttle2 - 0.5f;

  if (std::abs(correction_full) > 0.001f) {
    EXPECT_NEAR(correction_half, correction_full * 0.5f, 0.01f)
        << "Correction should scale linearly with stab_w";
  }
}

TEST_F(PitchCompensatorTest, ThrottleClamped_ToMinusOnePlusOne) {
  cfg_.pitch_comp.gain = 0.05f;  // Aggressive gain
  cfg_.pitch_comp.max_correction = 0.9f;
  comp_.Init(madgwick_, &imu_handler_);

  FeedPitchUp(30.0f, 500);
  float throttle = 0.9f;
  comp_.Process(cfg_, throttle, 1.0f);
  EXPECT_LE(throttle, 1.0f);
  EXPECT_GE(throttle, -1.0f);
}

TEST_F(PitchCompensatorTest, SymmetricCorrection_UpVsDown) {
  // Pitch up
  FeedPitchUp(15.0f, 500);
  float throttle_up = 0.5f;
  comp_.Process(cfg_, throttle_up, 1.0f);
  float correction_up = throttle_up - 0.5f;

  // Reset Madgwick and feed pitch down
  madgwick_.Reset();
  FeedPitchDown(15.0f, 500);
  float throttle_down = 0.5f;
  comp_.Process(cfg_, throttle_down, 1.0f);
  float correction_down = throttle_down - 0.5f;

  // Corrections should be roughly symmetric
  if (std::abs(correction_up) > 0.001f) {
    EXPECT_NEAR(correction_up, -correction_down, 0.05f)
        << "Pitch up and down should produce symmetric corrections";
  }
}
