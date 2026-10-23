#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include "vehicle_state_estimator.hpp"
#ifdef RC_PROFILE_LOOP
#include "mock_platform.hpp"
#endif

namespace rc_vehicle {
namespace {

class VehicleStateEstimatorTest : public ::testing::Test {
 protected:
  VehicleStateEstimatorTest() : estimator_(calib_, madgwick_, ekf_) {
    input_.filter.ekf_enabled = true;
    input_.filter.tilt_comp_enabled = true;
    input_.filter.madgwick_enabled = true;
    input_.dt_ms = 2;
    input_.ekf_available = true;

    sensors_.imu_enabled = true;
    sensors_.imu_data.az = 1.0f;
  }

  ImuCalibration calib_;
  MadgwickFilter madgwick_;
  VehicleEkf ekf_;
  VehicleStateEstimator estimator_;
  SensorSnapshot sensors_;
  VehicleStateEstimatorInput input_;
};

TEST_F(VehicleStateEstimatorTest, NoImuReturnsInvalidZeroEstimate) {
  sensors_.imu_enabled = false;

  const auto estimate = estimator_.Update(sensors_, input_);

  EXPECT_FALSE(estimate.imu_valid);
  EXPECT_FALSE(estimate.tilt_valid);
  EXPECT_FLOAT_EQ(estimate.forward_accel_g, 0.0f);
  EXPECT_FLOAT_EQ(estimate.speed_ms, 0.0f);
}

TEST_F(VehicleStateEstimatorTest, ZeroDtDoesNotAdvanceTiltOrEkf) {
  input_.dt_ms = 0;
  sensors_.imu_data.ax = 0.4f;

  const auto estimate = estimator_.Update(sensors_, input_);

  EXPECT_TRUE(estimate.imu_valid);
  EXPECT_FALSE(estimate.tilt_valid);
  EXPECT_FLOAT_EQ(estimate.speed_ms, 0.0f);
}

TEST_F(VehicleStateEstimatorTest, LevelStationaryProducesStableState) {
  VehicleStateEstimate estimate;
  for (int i = 0; i < 500; ++i) {
    estimate = estimator_.Update(sensors_, input_);
  }

  EXPECT_TRUE(estimate.imu_valid);
  EXPECT_TRUE(estimate.tilt_valid);
  EXPECT_NEAR(estimate.pitch_rad, 0.0f, 1e-4f);
  EXPECT_NEAR(estimate.roll_rad, 0.0f, 1e-4f);
  EXPECT_NEAR(estimate.speed_ms, 0.0f, 1e-3f);
  EXPECT_NEAR(estimate.forward_accel_g, 0.0f, 1e-4f);
  EXPECT_EQ(estimate.zupt_status, ZuptStatus::Applied);
  EXPECT_FALSE(estimate.ekf_diverged);
}

TEST_F(VehicleStateEstimatorTest, SubDeadzoneThrottleResidualAllowsZupt) {
  input_.filter.motor_deadzone = 0.05f;
  input_.applied_throttle = 0.049f;

  const auto estimate = estimator_.Update(sensors_, input_);

  EXPECT_EQ(estimate.zupt_status, ZuptStatus::Applied);
}

TEST_F(VehicleStateEstimatorTest, MotionCapableThrottleRejectsZupt) {
  input_.filter.motor_deadzone = 0.05f;
  input_.applied_throttle = 0.051f;

  const auto estimate = estimator_.Update(sensors_, input_);

  EXPECT_EQ(estimate.zupt_status, ZuptStatus::ThrottleRejected);
}

// LOS-385: колёса по мотор-модели разгоняются, корпус стоит (машина на
// подставке) — это пробуксовка.
TEST_F(VehicleStateEstimatorTest, WheelsDrivenWithBodyStillIsWheelspin) {
  // Сначала TiltEstimator сходится в покое: 3τ при corr_gain 0.5 Гц — 6 с.
  for (int i = 0; i < 3100; ++i) (void)estimator_.Update(sensors_, input_);
  input_.applied_throttle = 0.3f;
  input_.motor_model_throttle = 0.3f;
  VehicleStateEstimate estimate;
  for (int i = 0; i < 250; ++i) estimate = estimator_.Update(sensors_, input_);

  EXPECT_TRUE(estimate.wheels_driven);
  EXPECT_GT(estimate.wheel_slip_ratio, 0.5f);
  EXPECT_LT(estimate.ground_speed_ms, 0.5f);
}

// Код-ревью PR #402: без TiltEstimator гравитация на склоне остаётся в
// продольном ускорении, и детектору нечему верить.
TEST_F(VehicleStateEstimatorTest, NoWheelspinWithoutTiltCompensation) {
  input_.filter.tilt_comp_enabled = false;
  input_.applied_throttle = 0.3f;
  input_.motor_model_throttle = 0.3f;
  VehicleStateEstimate estimate;
  for (int i = 0; i < 250; ++i) estimate = estimator_.Update(sensors_, input_);

  EXPECT_FALSE(estimate.wheels_driven);
  EXPECT_FLOAT_EQ(estimate.wheel_slip_ratio, 0.0f);
}

// Код-ревью PR #402: мотор-модель не знает про нагрузку склона — в гору
// колёса по ней обгоняют корпус и при полном сцеплении.
TEST_F(VehicleStateEstimatorTest, NoWheelspinOnSlope) {
  sensors_.imu_data.ax = -0.17364818f;  // нос вверх 10°
  sensors_.imu_data.az = 0.98480775f;
  for (int i = 0; i < 3100; ++i) (void)estimator_.Update(sensors_, input_);
  input_.applied_throttle = 0.3f;
  input_.motor_model_throttle = 0.3f;
  VehicleStateEstimate estimate;
  for (int i = 0; i < 250; ++i) estimate = estimator_.Update(sensors_, input_);

  EXPECT_FALSE(estimate.wheels_driven);
  EXPECT_FLOAT_EQ(estimate.wheel_slip_ratio, 0.0f);
}

// Код-ревью PR #402: на пологом подъёме (внутри гейта по тангажу) машина
// разгоняется медленнее мотор-модели. С вычетом гравитации это выглядело бы
// как пробуксовка; тяга по акселерометру от уклона не зависит.
TEST_F(VehicleStateEstimatorTest, NoWheelspinOnGentleUphillLaunch) {
  constexpr float kG = 9.80665f;
  constexpr float kTauS = 0.9f;
  constexpr float kDt = 0.002f;
  const float slope_sin = std::sin(2.0f * 3.14159265f / 180.0f);
  sensors_.imu_data.ax = slope_sin;  // стоит на тормозе носом в подъём
  sensors_.imu_data.az = std::sqrt(1.0f - slope_sin * slope_sin);
  for (int i = 0; i < 3100; ++i) (void)estimator_.Update(sensors_, input_);

  input_.applied_throttle = 0.3f;
  input_.motor_model_throttle = 0.3f;
  const float target_ms = 8.0f * (0.3f - 0.05f) / (1.0f - 0.05f);
  float speed_ms = 0.0f;
  float max_ratio = 0.0f;
  bool driven = false;
  for (int i = 0; i < 1000; ++i) {
    const float traction_ms2 = (target_ms - speed_ms) / kTauS;
    sensors_.imu_data.ax = traction_ms2 / kG;
    speed_ms += (traction_ms2 - kG * slope_sin) * kDt;
    const auto estimate = estimator_.Update(sensors_, input_);
    max_ratio = std::max(max_ratio, estimate.wheel_slip_ratio);
    driven = driven || estimate.wheels_driven;
  }

  EXPECT_TRUE(driven);  // детектор работал, а не стоял за гейтом
  EXPECT_LT(max_ratio, 0.05f);
}

// Код-ревью PR #402: сразу после Reset() TiltEstimator держит pitch = 0, и
// на склоне гравитация ещё в продольном ускорении — детектор ждёт.
TEST_F(VehicleStateEstimatorTest, NoWheelspinWhileTiltAcquires) {
  input_.applied_throttle = 0.3f;
  input_.motor_model_throttle = 0.3f;
  VehicleStateEstimate estimate;
  for (int i = 0; i < 250; ++i) estimate = estimator_.Update(sensors_, input_);

  EXPECT_FALSE(estimate.wheels_driven);
  EXPECT_FLOAT_EQ(estimate.wheel_slip_ratio, 0.0f);
}

// LOS-380: после Reset() крен TiltEstimator = 0, и крен уклона сидит в ay;
// боковому ускорению верят, только когда оценка сошлась.
TEST_F(VehicleStateEstimatorTest, LateralAccelTrustedAfterTiltAcquires) {
  VehicleStateEstimate estimate = estimator_.Update(sensors_, input_);
  EXPECT_TRUE(estimate.tilt_valid);
  EXPECT_FALSE(estimate.lateral_accel_valid);
  // 3τ при tilt_corr_gain_hz = 0.5 — 6 с.
  for (int i = 0; i < 2900; ++i) estimate = estimator_.Update(sensors_, input_);
  EXPECT_FALSE(estimate.lateral_accel_valid);
  for (int i = 0; i < 200; ++i) estimate = estimator_.Update(sensors_, input_);
  EXPECT_TRUE(estimate.lateral_accel_valid);

  input_.filter.tilt_comp_enabled = false;
  estimate = estimator_.Update(sensors_, input_);
  EXPECT_FALSE(estimate.lateral_accel_valid);
  input_.filter.tilt_comp_enabled = true;
  estimate = estimator_.Update(sensors_, input_);
  EXPECT_FALSE(estimate.lateral_accel_valid);
}

// LOS-378: руль влево до упора, а машина не поворачивает — шины не держат,
// и EKF перестаёт гасить боковую скорость; yaw rate по рулю — держат.
TEST_F(VehicleStateEstimatorTest, YawRateAgainstSteeringIsGripLoss) {
  input_.applied_steering = 0.7f;
  VehicleStateEstimate estimate;
  for (int i = 0; i < 100; ++i) {
    ekf_.SetState(2.0f, 0.0f, ekf_.GetYawRate());
    estimate = estimator_.Update(sensors_, input_);
  }
  EXPECT_TRUE(estimate.lateral_grip_lost);
  EXPECT_TRUE(ekf_.IsLateralGripLost());

  const float grip_rate_rps = 2.0f *
                              std::tan(0.7f * GripLossDetector::kMaxSteerRad) /
                              GripLossDetector::kWheelbaseM;
  sensors_.filtered_gz = grip_rate_rps * 180.0f / 3.14159265f;
  for (int i = 0; i < 200; ++i) {
    ekf_.SetState(2.0f, 0.0f, ekf_.GetYawRate());
    estimate = estimator_.Update(sensors_, input_);
  }
  EXPECT_FALSE(estimate.lateral_grip_lost);
}

TEST_F(VehicleStateEstimatorTest, TiltRunsWhenEkfIsDisabled) {
  input_.filter.ekf_enabled = false;
  sensors_.imu_data.ax = -0.17364818f;
  sensors_.imu_data.az = 0.98480775f;

  VehicleStateEstimate estimate;
  for (int i = 0; i < 3000; ++i) {
    estimate = estimator_.Update(sensors_, input_);
  }

  EXPECT_TRUE(estimate.tilt_valid);
  EXPECT_GT(estimate.pitch_rad, 0.1f);
  EXPECT_NEAR(estimate.speed_ms, 0.0f, 1e-5f);
  EXPECT_NEAR(estimate.forward_accel_g, 0.0f, 0.03f);
}

TEST_F(VehicleStateEstimatorTest, MagnetometerHeadingUpdatesEkfYaw) {
  sensors_.mag_enabled = true;
  sensors_.heading_deg = 90.0f;

  VehicleStateEstimate estimate;
  for (int i = 0; i < 20; ++i) {
    sensors_.mag_sample_sequence = static_cast<uint32_t>(i + 1);
    estimate = estimator_.Update(sensors_, input_);
  }

  EXPECT_NEAR(estimate.yaw_rad, 3.14159265f / 2.0f, 0.05f);
}

TEST_F(VehicleStateEstimatorTest, AppliesEachMagSampleOnlyOnce) {
  sensors_.mag_enabled = true;
  sensors_.heading_deg = 90.0f;
  sensors_.mag_sample_sequence = 1;

  const auto first = estimator_.Update(sensors_, input_);
  VehicleStateEstimate duplicate = first;
  for (int i = 0; i < 20; ++i) {
    duplicate = estimator_.Update(sensors_, input_);
  }
  EXPECT_NEAR(duplicate.yaw_rad, first.yaw_rad, 1e-5f);

  sensors_.mag_sample_sequence = 2;
  const auto fresh = estimator_.Update(sensors_, input_);
  EXPECT_GT(fresh.yaw_rad, duplicate.yaw_rad);
}

TEST_F(VehicleStateEstimatorTest, RejectedMagSampleDoesNotUpdateEkfYaw) {
  sensors_.mag_enabled = true;
  sensors_.mag_rejected = true;
  sensors_.heading_deg = 90.0f;
  sensors_.mag_sample_sequence = 1;

  const auto estimate = estimator_.Update(sensors_, input_);

  EXPECT_NEAR(estimate.yaw_rad, 0.0f, 1e-6f);
}

TEST_F(VehicleStateEstimatorTest, MotorModelAnchorsForwardSpeed) {
  input_.filter.motor_model_enabled = true;
  input_.filter.motor_deadzone = 0.1f;
  input_.filter.motor_speed_gain = 5.0f;
  input_.filter.speed_meas_noise = 0.1f;
  input_.applied_throttle = 0.5f;
  input_.motor_model_throttle = 0.5f;

  VehicleStateEstimate estimate;
  for (int i = 0; i < 1500; ++i) {  // 3 с — больше 3τ инерции мотор-модели
    estimate = estimator_.Update(sensors_, input_);
  }

  EXPECT_GT(estimate.vx_ms, 1.0f);
  EXPECT_LT(estimate.vx_ms, VehicleEkf::kMaxSpeedMs);
}

TEST_F(VehicleStateEstimatorTest, MotorModelAnchorFollowsMotorLag) {
  // LOS-378: машина набирает скорость с τ ≈ 0.9 с, и якорь не должен сразу
  // выдавать установившуюся скорость, иначе EKF vx обгоняет машину.
  input_.filter.motor_model_enabled = true;
  input_.filter.motor_deadzone = 0.0f;
  input_.filter.motor_speed_gain = 8.0f;
  input_.motor_model_throttle = 0.5f;  // цель 4 м/с

  estimator_.Update(sensors_, input_);
  EXPECT_FLOAT_EQ(estimator_.GetMotorModelTargetMs(), 4.0f);
  EXPECT_LT(ekf_.GetLastSpeedMeas(), 0.05f);

  for (int i = 1; i < 450; ++i) estimator_.Update(sensors_, input_);  // 0.9 с
  EXPECT_NEAR(ekf_.GetLastSpeedMeas(), 4.0f * (1.0f - std::exp(-1.0f)), 0.05f);

  for (int i = 0; i < 2500; ++i) estimator_.Update(sensors_, input_);
  EXPECT_NEAR(ekf_.GetLastSpeedMeas(), 4.0f, 0.01f);
}

TEST_F(VehicleStateEstimatorTest, MotorModelLagRestartsFromEkfAfterReset) {
  input_.filter.motor_model_enabled = true;
  input_.filter.motor_deadzone = 0.0f;
  input_.filter.motor_speed_gain = 8.0f;
  input_.motor_model_throttle = 0.5f;
  for (int i = 0; i < 3000; ++i) estimator_.Update(sensors_, input_);
  ASSERT_GT(ekf_.GetLastSpeedMeas(), 3.9f);

  ekf_.Reset();
  estimator_.OnEkfReset();
  input_.motor_model_throttle = 0.0f;
  estimator_.Update(sensors_, input_);
  EXPECT_NEAR(ekf_.GetLastSpeedMeas(), 0.0f, 0.01f)
      << "после сброса EKF якорь не должен тянуть vx к прежней скорости";
}

TEST_F(VehicleStateEstimatorTest, TiltAnchorSettlesAfterMotorModelReturns) {
  // LOS-369: пока якоря не было, разгон мог завалить pitch. После возврата
  // мотор-модели оценка считается якорной только через 3τ = 3 / corr_gain.
  input_.filter.motor_model_enabled = true;
  input_.filter.tilt_corr_gain_hz = 1.0f;  // выдержка 3 с = 1500 тиков
  EXPECT_TRUE(estimator_.Update(sensors_, input_).tilt_accel_anchored)
      << "после Reset() на старте выдержка не нужна";

  input_.filter.motor_model_enabled = false;
  for (int i = 0; i < 100; ++i) {
    EXPECT_FALSE(estimator_.Update(sensors_, input_).tilt_accel_anchored);
  }

  input_.filter.motor_model_enabled = true;
  for (int i = 0; i < 1499; ++i) {
    ASSERT_FALSE(estimator_.Update(sensors_, input_).tilt_accel_anchored)
        << "тик " << i;
  }
  EXPECT_TRUE(estimator_.Update(sensors_, input_).tilt_accel_anchored);
}

TEST_F(VehicleStateEstimatorTest,
       TiltAnchorSettleCountsOnlyAcceptedCorrections) {
  // LOS-369: пока accel-коррекция отбрасывается гейтом 1g (ухабы, ошибка
  // мотор-модели), устаревший pitch не сходится — выдержка не идёт.
  input_.filter.motor_model_enabled = true;
  input_.filter.tilt_corr_gain_hz = 1.0f;
  (void)estimator_.Update(sensors_, input_);
  input_.filter.motor_model_enabled = false;
  (void)estimator_.Update(sensors_, input_);

  input_.filter.motor_model_enabled = true;
  sensors_.imu_data.az = 1.6f;  // |a| вне гейта
  for (int i = 0; i < 3000; ++i) {
    ASSERT_FALSE(estimator_.Update(sensors_, input_).tilt_accel_anchored)
        << "тик " << i;
  }
  sensors_.imu_data.az = 1.0f;
  bool anchored = false;
  for (int i = 0; i < 1500 && !anchored; ++i) {
    anchored = estimator_.Update(sensors_, input_).tilt_accel_anchored;
  }
  EXPECT_TRUE(anchored);
}

TEST_F(VehicleStateEstimatorTest, TiltAnchorSettleTracksGainChanges) {
  // LOS-369: выдержка считается в постоянных времени, а не в секундах —
  // снижение tilt_corr_gain_hz на ходу её удлиняет.
  input_.filter.motor_model_enabled = true;
  input_.filter.tilt_corr_gain_hz = 5.0f;  // 3τ = 0.6 с
  (void)estimator_.Update(sensors_, input_);
  input_.filter.motor_model_enabled = false;
  (void)estimator_.Update(sensors_, input_);

  input_.filter.motor_model_enabled = true;
  (void)estimator_.Update(sensors_, input_);
  input_.filter.tilt_corr_gain_hz = 0.5f;  // 3τ = 6 с
  for (int i = 0; i < 2000; ++i) {         // 4 с
    ASSERT_FALSE(estimator_.Update(sensors_, input_).tilt_accel_anchored)
        << "тик " << i;
  }
  bool anchored = false;
  for (int i = 0; i < 1500 && !anchored; ++i) {
    anchored = estimator_.Update(sensors_, input_).tilt_accel_anchored;
  }
  EXPECT_TRUE(anchored);
}

TEST_F(VehicleStateEstimatorTest, SteadyDriftDoesNotTiltPitch) {
  // LOS-380: в установившемся заносе на ровном v̇x = 0, а акселерометр по X
  // видит −r·vy. Якорь, считавший линейное ускорение как Δvx/dt, принимал
  // его за наклон (≈7° при r = 1.2 рад/с, vy = 1 м/с).
  constexpr float kG = 9.80665f;
  constexpr float kVx = 3.0f;
  constexpr float kVy = 1.0f;
  constexpr float kR = 1.2f;
  input_.filter.motor_model_enabled = true;
  input_.filter.tilt_corr_gain_hz = 5.0f;
  sensors_.imu_data.ax = -kR * kVy / kG;
  sensors_.imu_data.ay = kR * kVx / kG;
  sensors_.imu_data.gz = kR * 180.0f / static_cast<float>(M_PI);
  sensors_.filtered_gz = sensors_.imu_data.gz;

  VehicleStateEstimate estimate;
  for (int i = 0; i < 1000; ++i) {  // 2 с = 10τ коррекции
    ekf_.SetState(kVx, kVy, kR);
    estimate = estimator_.Update(sensors_, input_);
  }

  ASSERT_TRUE(estimate.tilt_accel_anchored);
  // Без r·vy в якоре pitch уходил на 6.6°.
  EXPECT_NEAR(estimate.pitch_rad, 0.0f,
              1.0f * static_cast<float>(M_PI) / 180.0f);
}

TEST_F(VehicleStateEstimatorTest, CounterfactualMotorModelIsNotTiltAnchor) {
  // LOS-369: пока Kids speed limiter срезает PWM, ускорение мотор-модели
  // counterfactual — pitch по нему для компенсации не годится.
  input_.filter.motor_model_enabled = true;
  input_.motor_model_counterfactual = true;
  EXPECT_FALSE(estimator_.Update(sensors_, input_).tilt_accel_anchored);
  input_.motor_model_counterfactual = false;
  EXPECT_FALSE(estimator_.Update(sensors_, input_).tilt_accel_anchored)
      << "после вмешательства лимитера нужна выдержка якоря";
}

TEST_F(VehicleStateEstimatorTest, FrameChangeResetsTiltHistory) {
  sensors_.imu_data.ax = -0.5f;
  sensors_.imu_data.az = 0.8660254f;
  for (int i = 0; i < 500; ++i) {
    (void)estimator_.Update(sensors_, input_);
  }

  estimator_.OnReferenceFrameChanged();
  sensors_.imu_data.ax = 0.0f;
  sensors_.imu_data.az = 1.0f;
  const auto estimate = estimator_.Update(sensors_, input_);

  EXPECT_TRUE(estimate.tilt_valid);
  EXPECT_NEAR(estimate.pitch_rad, 0.0f, 1e-3f);
}

TEST_F(VehicleStateEstimatorTest, RefreshEkfFieldsDropsPreResetState) {
  VehicleStateEstimate estimate;
  estimate.pitch_rad = 0.25f;
  estimate.forward_accel_g = 0.4f;
  ekf_.SetState(4.0f, 3.0f, 0.5f);
  estimator_.RefreshEkfFields(estimate);
  ASSERT_FLOAT_EQ(estimate.speed_ms, 5.0f);

  ekf_.Reset();
  estimator_.RefreshEkfFields(estimate);

  EXPECT_FLOAT_EQ(estimate.vx_ms, 0.0f);
  EXPECT_FLOAT_EQ(estimate.vy_ms, 0.0f);
  EXPECT_FLOAT_EQ(estimate.speed_ms, 0.0f);
  EXPECT_FLOAT_EQ(estimate.yaw_rate_rps, 0.0f);
  EXPECT_FLOAT_EQ(estimate.pitch_rad, 0.25f);
  EXPECT_FLOAT_EQ(estimate.forward_accel_g, 0.4f);
}

#ifdef RC_PROFILE_LOOP
TEST_F(VehicleStateEstimatorTest, ProfileRecordsConditionalStagesAndResets) {
  testing::FakePlatform clock;
  clock.SetTimeUsReadIncrement(1);
  input_.filter.motor_model_enabled = true;
  input_.filter.nhc_enabled = true;
  sensors_.mag_enabled = true;
  sensors_.mag_sample_sequence = 1;

  (void)estimator_.Update(sensors_, input_, &clock);

  const auto& profile = estimator_.GetProfileStats();
  EXPECT_EQ(profile.com_offset.calls, 1u);
  EXPECT_EQ(profile.rotate.calls, 1u);
  EXPECT_EQ(profile.tilt.calls, 1u);
  EXPECT_EQ(profile.imu.calls, 1u);
  EXPECT_EQ(profile.speed.calls, 1u);
  EXPECT_EQ(profile.nhc.calls, 1u);
  EXPECT_EQ(profile.heading.calls, 1u);
  EXPECT_EQ(profile.com_offset.total_us, 1u);
  EXPECT_EQ(profile.heading.max_us, 1u);

  estimator_.ResetProfileStats();

  EXPECT_EQ(estimator_.GetProfileStats().com_offset.calls, 0u);
  EXPECT_EQ(estimator_.GetProfileStats().heading.total_us, 0u);
}

TEST_F(VehicleStateEstimatorTest, ProfileDoesNotCountSkippedStages) {
  testing::FakePlatform clock;
  clock.SetTimeUsReadIncrement(1);
  sensors_.imu_enabled = false;

  (void)estimator_.Update(sensors_, input_, &clock);

  const auto& profile = estimator_.GetProfileStats();
  EXPECT_EQ(profile.com_offset.calls, 1u);
  EXPECT_EQ(profile.rotate.calls, 0u);
  EXPECT_EQ(profile.tilt.calls, 0u);
  EXPECT_EQ(profile.imu.calls, 0u);
  EXPECT_EQ(profile.speed.calls, 0u);
  EXPECT_EQ(profile.nhc.calls, 0u);
  EXPECT_EQ(profile.heading.calls, 0u);
}
#endif

}  // namespace
}  // namespace rc_vehicle
