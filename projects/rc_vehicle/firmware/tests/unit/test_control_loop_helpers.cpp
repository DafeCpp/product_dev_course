#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <cmath>

#include "control_loop_helpers.hpp"
#include "imu_calibration.hpp"
#include "mock_platform.hpp"
#include "motion_driver.hpp"
#include "stabilization_manager.hpp"

using namespace rc_vehicle;
using namespace rc_vehicle::testing;
using ::testing::_;
using ::testing::AtLeast;

// ═══════════════════════════════════════════════════════════════════════════
// SelectControlSource
// ═══════════════════════════════════════════════════════════════════════════

TEST(SelectControlSourceTest, NeitherActive_NoChange) {
  SensorSnapshot s;
  s.rc_active = false;
  s.wifi_active = false;
  float thr = 0.5f, steer = 0.3f;
  bool result = SelectControlSource(s, thr, steer);
  EXPECT_FALSE(result);
  EXPECT_FLOAT_EQ(thr, 0.5f);
  EXPECT_FLOAT_EQ(steer, 0.3f);
}

TEST(SelectControlSourceTest, RcActiveNoCmd_NoChange) {
  SensorSnapshot s;
  s.rc_active = true;
  s.rc_cmd = std::nullopt;
  float thr = 0.5f, steer = 0.3f;
  EXPECT_FALSE(SelectControlSource(s, thr, steer));
  EXPECT_FLOAT_EQ(thr, 0.5f);
}

TEST(SelectControlSourceTest, RcActive_SetsCommands) {
  SensorSnapshot s;
  s.rc_active = true;
  s.rc_cmd = RcCommand{0.8f, -0.4f};
  float thr = 0.0f, steer = 0.0f;
  EXPECT_TRUE(SelectControlSource(s, thr, steer));
  EXPECT_NEAR(thr, (0.8f - 0.08f) / 0.92f, 1e-6f);
  EXPECT_FLOAT_EQ(steer, -0.4f);
}

TEST(SelectControlSourceTest, RcNeutralNoise_IsCollapsedToZero) {
  SensorSnapshot s;
  s.rc_active = true;
  s.rc_cmd = RcCommand{0.08f, 0.0f};
  float thr = 1.0f, steer = 0.0f;
  EXPECT_TRUE(SelectControlSource(s, thr, steer));
  EXPECT_FLOAT_EQ(thr, 0.0f);

  s.rc_cmd = RcCommand{-0.08f, 0.0f};
  EXPECT_TRUE(SelectControlSource(s, thr, steer));
  EXPECT_FLOAT_EQ(thr, 0.0f);
}

TEST(SelectControlSourceTest, RcAboveDeadzone_IsRescaledContinuously) {
  SensorSnapshot s;
  s.rc_active = true;
  s.rc_cmd = RcCommand{0.081f, 0.0f};
  float thr = 0.0f, steer = 0.0f;
  EXPECT_TRUE(SelectControlSource(s, thr, steer));
  EXPECT_NEAR(thr, (0.081f - 0.08f) / 0.92f, 1e-6f);

  s.rc_cmd = RcCommand{-0.081f, 0.0f};
  EXPECT_TRUE(SelectControlSource(s, thr, steer));
  EXPECT_NEAR(thr, -(0.081f - 0.08f) / 0.92f, 1e-6f);
}

TEST(SelectControlSourceTest, WifiActive_SetsCommands) {
  SensorSnapshot s;
  s.wifi_active = true;
  s.wifi_cmd = RcCommand{0.3f, 0.6f};
  float thr = 0.0f, steer = 0.0f;
  EXPECT_TRUE(SelectControlSource(s, thr, steer));
  EXPECT_FLOAT_EQ(thr, 0.3f);
  EXPECT_FLOAT_EQ(steer, 0.6f);
}

TEST(SelectControlSourceTest, LowWifiThrottle_IsPreserved) {
  SensorSnapshot s;
  s.wifi_active = true;
  s.wifi_cmd = RcCommand{0.03f, 0.0f};
  float thr = 0.0f, steer = 0.0f;
  EXPECT_TRUE(SelectControlSource(s, thr, steer));
  EXPECT_FLOAT_EQ(thr, 0.03f);
}

TEST(SelectControlSourceTest, RcPriorityOverWifi) {
  SensorSnapshot s;
  s.rc_active = true;
  s.rc_cmd = RcCommand{1.0f, 0.0f};
  s.wifi_active = true;
  s.wifi_cmd = RcCommand{-1.0f, -1.0f};
  float thr = 0.0f, steer = 0.0f;
  EXPECT_TRUE(SelectControlSource(s, thr, steer));
  EXPECT_FLOAT_EQ(thr, 1.0f);  // RC wins; full scale stays full scale
  EXPECT_FLOAT_EQ(steer, 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// ComputeForwardAccelG (LOS-245)
//
// GetForwardAccel() вычитает КОНСТАНТНЫЙ RestDownVec() — неявно считает машину
// горизонтальной, и при тангаже отдаёт наклон вместо ускорения: в ось «вперёд»
// протекает sin(pitch)·g. Порог accel-лимитера Kids Mode 0.15 g подделывается
// тангажом всего в 8.6°, а p95 тангажа по логу ночного заезда — 14.7°.
// ComputeForwardAccelG() снимает гравитацию по ТЕКУЩЕЙ оценке тангажа.
// ═══════════════════════════════════════════════════════════════════════════

class ForwardAccelTest : public ::testing::Test {
 protected:
  ImuCalibration calib;

  void SetUp() override {
    // Горизонтальная калибровка: СК датчика === СК машины, чтобы тест мерил
    // именно компенсацию тангажа, а не ротацию монтажа.
    ImuCalibData d{};
    d.valid = true;
    d.gravity_valid = true;
    d.gravity_vec[2] = 1.f;
    d.accel_forward_vec[0] = 1.f;
    calib.SetData(d);
  }

  /** Показания акселерометра [g] в покое при заданном тангаже.
   *  Нос ВНИЗ = pitch < 0 → ax > 0 (клевок читается как «разгон»). */
  static ImuData AtRestWithPitch(float pitch_rad) {
    ImuData d{};
    d.ax = -std::sin(pitch_rad);
    d.az = std::cos(pitch_rad);
    return d;
  }

  /** Тот же путь, что в ControlLoopProcessor::UpdateSensorsAndEkf(). */
  float Compute(const ImuData& sensor_imu, float pitch_rad, bool tilt_valid) {
    ImuData veh = sensor_imu;
    calib.RotateToVehicleFrame(veh);
    return ComputeForwardAccelG(calib, veh, sensor_imu, pitch_rad, tilt_valid);
  }
};

TEST_F(ForwardAccelTest, StaticNoseDownPitch_TiltCompensated_ReturnsZero) {
  constexpr float kPitch = -15.f * 3.14159265358979f / 180.f;  // клевок вниз
  const ImuData imu = AtRestWithPitch(kPitch);

  // Дискриминативность: старое поведение выдаёт ускорение выше порога
  // accel-лимитера (0.15 g) на СТОЯЩЕЙ машине — ровно ложное срабатывание,
  // найденное в логе.
  EXPECT_GT(calib.GetForwardAccel(imu), 0.15f);

  // С компенсацией по тангажу — покой распознан как покой.
  EXPECT_NEAR(Compute(imu, kPitch, /*tilt_valid=*/true), 0.0f, 1e-4f);
}

TEST_F(ForwardAccelTest, StaticNoseUpPitch_TiltCompensated_ReturnsZero) {
  constexpr float kPitch = 15.f * 3.14159265358979f / 180.f;
  const ImuData imu = AtRestWithPitch(kPitch);

  EXPECT_LT(calib.GetForwardAccel(imu), -0.15f);
  EXPECT_NEAR(Compute(imu, kPitch, /*tilt_valid=*/true), 0.0f, 1e-4f);
}

TEST_F(ForwardAccelTest, ExtremePitch_TiltCompensated_ReturnsZero) {
  // Максимум тангажа из лога — 58.3°, ниже клампа TiltEstimator (60°).
  constexpr float kPitch = -58.3f * 3.14159265358979f / 180.f;
  const ImuData imu = AtRestWithPitch(kPitch);

  EXPECT_GT(calib.GetForwardAccel(imu), 0.8f);
  EXPECT_NEAR(Compute(imu, kPitch, /*tilt_valid=*/true), 0.0f, 1e-4f);
}

TEST_F(ForwardAccelTest, LevelSurface_MeasuresRealAcceleration) {
  ImuData imu{};
  imu.ax = 0.2f;
  imu.az = 1.0f;
  // На горизонтали компенсация — тождество: полезный сигнал не трогаем.
  EXPECT_NEAR(Compute(imu, 0.0f, /*tilt_valid=*/true), 0.2f, 1e-5f);
  EXPECT_NEAR(calib.GetForwardAccel(imu), 0.2f, 1e-5f);
}

TEST_F(ForwardAccelTest, AccelerationOnSlope_SeparatedFromTilt) {
  // Разгон 0.2 g на подъёме 15°: ускорение видно, наклон снят.
  constexpr float kPitch = 15.f * 3.14159265358979f / 180.f;
  ImuData imu = AtRestWithPitch(kPitch);
  imu.ax += 0.2f;

  EXPECT_NEAR(Compute(imu, kPitch, /*tilt_valid=*/true), 0.2f, 1e-4f);
}

TEST_F(ForwardAccelTest, TiltInvalid_FallsBackToLegacyBehaviour) {
  constexpr float kPitch = -15.f * 3.14159265358979f / 180.f;
  const ImuData imu = AtRestWithPitch(kPitch);

  // Без оценки ориентации деградируем ровно до прежнего поведения — не хуже.
  EXPECT_FLOAT_EQ(Compute(imu, kPitch, /*tilt_valid=*/false),
                  calib.GetForwardAccel(imu));
}

// ═══════════════════════════════════════════════════════════════════════════
// BuildAutoDriveInput
// ═══════════════════════════════════════════════════════════════════════════

TEST(BuildAutoDriveInputTest, ImuDisabled_ZeroAccel) {
  SensorSnapshot s;
  s.imu_enabled = false;
  s.rc_active = true;
  auto ad = BuildAutoDriveInput(s, 0.25f, 2);
  EXPECT_TRUE(ad.rc_active);
  EXPECT_FALSE(ad.imu_enabled);
  EXPECT_FLOAT_EQ(ad.fwd_accel, 0.0f);
  EXPECT_FLOAT_EQ(ad.accel_mag, 1.0f);  // default — safe value when IMU absent
  EXPECT_FLOAT_EQ(ad.dt_sec, 0.002f);
}

TEST(BuildAutoDriveInputTest, ImuEnabled_AccelMagComputed) {
  SensorSnapshot s;
  s.imu_enabled = true;
  s.imu_data.ax = 0.0f;
  s.imu_data.ay = 0.0f;
  s.imu_data.az = 1.0f;
  s.filtered_gz = 5.0f;
  auto ad = BuildAutoDriveInput(s, 0.125f, 4);
  EXPECT_TRUE(ad.imu_enabled);
  EXPECT_FLOAT_EQ(ad.fwd_accel, 0.125f);
  EXPECT_NEAR(ad.accel_mag, 1.0f, 1e-5f);
  EXPECT_FLOAT_EQ(ad.gyro_z, 5.0f);
  EXPECT_FLOAT_EQ(ad.cal_ax, 0.0f);
  EXPECT_FLOAT_EQ(ad.cal_ay, 0.0f);
  EXPECT_FLOAT_EQ(ad.dt_sec, 0.004f);
}

TEST(BuildAutoDriveInputTest, AccelMagDiagonal) {
  SensorSnapshot s;
  s.imu_enabled = true;
  s.imu_data.ax = 1.0f;
  s.imu_data.ay = 1.0f;
  s.imu_data.az = 1.0f;
  auto ad = BuildAutoDriveInput(s, 0.0f, 2);
  EXPECT_NEAR(ad.accel_mag, std::sqrt(3.0f), 1e-5f);
}

TEST_F(ForwardAccelTest, PitchChangeWithoutAccelerationKeepsBreakawayRamp) {
  MotionDriver driver;
  driver.Start(MotionDriver::Config{});

  SensorSnapshot sensors;
  sensors.imu_enabled = true;
  sensors.imu_data.az = 1.0f;

  // Settle замеряется на горизонтали, как перед началом авто-процедуры.
  for (int i = 0; i < 50; ++i) {
    const auto input = BuildAutoDriveInput(
        sensors, Compute(sensors.imu_data, 0.0f, /*tilt_valid=*/true), 2);
    driver.Update(input.fwd_accel, input.accel_mag, input.gyro_z, input.dt_sec);
  }
  ASSERT_EQ(driver.GetPhase(), MotionPhase::Accelerate);
  EXPECT_NEAR(driver.GetAccelBaseline(), 0.0f, 1e-5f);

  // Уже после baseline кузов меняет тангаж на 5 градусов. Старый путь через
  // GetForwardAccel() видит здесь sin(5 deg)=0.087g > 0.03g и после 25 тиков
  // ложно подтверждает breakaway. Компенсированный сигнал остаётся нулевым,
  // поэтому open-loop ramp должен продолжить линейный рост до 0.1 throttle.
  constexpr float kPitch = -5.f * 3.14159265358979f / 180.f;
  sensors.imu_data = AtRestWithPitch(kPitch);
  ASSERT_GT(calib.GetForwardAccel(sensors.imu_data), 0.03f);

  float throttle = 0.0f;
  for (int i = 0; i < 100; ++i) {
    const auto input = BuildAutoDriveInput(
        sensors, Compute(sensors.imu_data, kPitch, /*tilt_valid=*/true), 2);
    throttle = driver.Update(input.fwd_accel, input.accel_mag, input.gyro_z,
                             input.dt_sec);
  }
  EXPECT_NEAR(throttle, 0.1f, 1e-4f)
      << "изменение тангажа было принято за отрыв";
}

TEST_F(ForwardAccelTest, RealAccelerationOnSlopeStillTriggersBreakaway) {
  MotionDriver driver;
  driver.Start(MotionDriver::Config{});

  SensorSnapshot sensors;
  sensors.imu_enabled = true;
  sensors.imu_data.az = 1.0f;
  for (int i = 0; i < 50; ++i) {
    const auto input = BuildAutoDriveInput(
        sensors, Compute(sensors.imu_data, 0.0f, /*tilt_valid=*/true), 2);
    driver.Update(input.fwd_accel, input.accel_mag, input.gyro_z, input.dt_sec);
  }

  constexpr float kPitch = -5.f * 3.14159265358979f / 180.f;
  sensors.imu_data = AtRestWithPitch(kPitch);
  sensors.imu_data.ax += 0.1f;

  float throttle = 0.0f;
  for (int i = 0; i < 100; ++i) {
    const auto input = BuildAutoDriveInput(
        sensors, Compute(sensors.imu_data, kPitch, /*tilt_valid=*/true), 2);
    ASSERT_NEAR(input.fwd_accel, 0.1f, 1e-4f);
    throttle = driver.Update(input.fwd_accel, input.accel_mag, input.gyro_z,
                             input.dt_sec);
  }
  EXPECT_LT(throttle, 0.04f) << "реальное ускорение 0.1g не подтвердило отрыв";
}

// ═══════════════════════════════════════════════════════════════════════════
// CorrectImuForComOffset
// ═══════════════════════════════════════════════════════════════════════════

TEST(CorrectImuForComOffsetTest, ImuDisabled_ReturnsPrevGz) {
  SensorSnapshot s;
  s.imu_enabled = false;
  ImuCalibration calib;
  float prev = 0.5f;
  float result = CorrectImuForComOffset(s, calib, prev, 2);
  EXPECT_FLOAT_EQ(result, 0.5f);
}

TEST(CorrectImuForComOffsetTest, DtZero_ReturnsPrevGz) {
  SensorSnapshot s;
  s.imu_enabled = true;
  s.filtered_gz = 10.0f;
  ImuCalibration calib;
  float result = CorrectImuForComOffset(s, calib, 0.2f, 0);
  EXPECT_FLOAT_EQ(result, 0.2f);
}

TEST(CorrectImuForComOffsetTest, ImuEnabled_ReturnsUpdatedGz) {
  SensorSnapshot s;
  s.imu_enabled = true;
  s.filtered_gz = 90.0f;  // 90 dps
  ImuCalibration calib;
  constexpr float kDeg2Rad = 3.14159265358979f / 180.0f;
  float result = CorrectImuForComOffset(s, calib, 0.0f, 2);
  EXPECT_NEAR(result, 90.0f * kDeg2Rad, 1e-4f);
}

// ═══════════════════════════════════════════════════════════════════════════
// HandleAutoDriveCompletion
// ═══════════════════════════════════════════════════════════════════════════

class HandleAutoDriveTest : public ::testing::Test {
 protected:
  MockPlatform platform_;
  MadgwickFilter madgwick_;
  YawRateController yaw_ctrl_;
  SlipAngleController slip_ctrl_;
  StabilizationManager stab_mgr_{platform_, madgwick_, yaw_ctrl_, slip_ctrl_,
                                 nullptr};
  ImuCalibration imu_calib_;
};

TEST_F(HandleAutoDriveTest, NothingCompleted_NoSideEffects) {
  AutoDriveOutput ad;
  ad.trim_completed = false;
  ad.com_completed = false;
  EXPECT_CALL(platform_, Log(_, _)).Times(0);
  HandleAutoDriveCompletion(ad, &stab_mgr_, imu_calib_, platform_);
}

TEST_F(HandleAutoDriveTest, TrimCompleted_Valid_UpdatesConfig) {
  AutoDriveOutput ad;
  ad.trim_completed = true;
  ad.trim_result.valid = true;
  ad.trim_result.trim = 0.05f;
  // StabilizationManager::SetConfig may also log → accept ≥1 Info calls
  EXPECT_CALL(platform_, Log(LogLevel::Info, _)).Times(AtLeast(1));
  EXPECT_CALL(platform_, SaveCalib(_)).Times(::testing::AnyNumber());
  EXPECT_CALL(platform_, SaveStabilizationConfig(_))
      .Times(::testing::AnyNumber());
  HandleAutoDriveCompletion(ad, &stab_mgr_, imu_calib_, platform_);
  EXPECT_FLOAT_EQ(stab_mgr_.GetConfig().steering_trim, 0.05f);
}

TEST_F(HandleAutoDriveTest, TrimCompleted_Invalid_LogsWarning) {
  AutoDriveOutput ad;
  ad.trim_completed = true;
  ad.trim_result.valid = false;
  EXPECT_CALL(platform_, Log(LogLevel::Warning, _)).Times(1);
  HandleAutoDriveCompletion(ad, &stab_mgr_, imu_calib_, platform_);
}

TEST_F(HandleAutoDriveTest, TrimCompleted_NullStabMgr_NoUpdate) {
  AutoDriveOutput ad;
  ad.trim_completed = true;
  ad.trim_result.valid = true;
  ad.trim_result.trim = 0.1f;
  EXPECT_CALL(platform_, Log(_, _)).Times(0);
  // stab_mgr = nullptr → silent no-op
  HandleAutoDriveCompletion(ad, nullptr, imu_calib_, platform_);
}

TEST_F(HandleAutoDriveTest, ComCompleted_Valid_UpdatesCalib) {
  AutoDriveOutput ad;
  ad.com_completed = true;
  ad.com_result.valid = true;
  ad.com_result.rx = 0.05f;
  ad.com_result.ry = -0.03f;
  EXPECT_CALL(platform_, Log(LogLevel::Info, _)).Times(1);
  EXPECT_CALL(platform_, SaveComOffset(_)).Times(1);
  HandleAutoDriveCompletion(ad, &stab_mgr_, imu_calib_, platform_);
  const auto& data = imu_calib_.GetData();
  EXPECT_FLOAT_EQ(data.com_offset[0], 0.05f);
  EXPECT_FLOAT_EQ(data.com_offset[1], -0.03f);
}

TEST_F(HandleAutoDriveTest, ComCompleted_Invalid_LogsWarning) {
  AutoDriveOutput ad;
  ad.com_completed = true;
  ad.com_result.valid = false;
  EXPECT_CALL(platform_, Log(LogLevel::Warning, _)).Times(1);
  HandleAutoDriveCompletion(ad, &stab_mgr_, imu_calib_, platform_);
}

TEST_F(HandleAutoDriveTest, SpeedCalCompleted_Valid_ConvertsGainForDeadzone) {
  // Калибровка: v = gain·thr; модель: v = gain·(thr−dz)/(1−dz).
  // gain_model = mean_speed·(1−dz)/(target−dz) = 2.4·0.95/0.25 = 9.12.
  AutoDriveOutput ad;
  ad.speed_cal_completed = true;
  ad.speed_cal_result.valid = true;
  ad.speed_cal_result.target_throttle = 0.3f;
  ad.speed_cal_result.mean_speed_ms = 2.4f;
  ad.speed_cal_result.speed_gain =
      8.0f;  // сырое v/thr — не должно попасть в cfg
  auto cfg = stab_mgr_.GetConfig();
  cfg.filter.motor_deadzone = 0.05f;
  stab_mgr_.SetConfig(cfg, false);
  EXPECT_CALL(platform_, Log(LogLevel::Info, _)).Times(AtLeast(1));
  EXPECT_CALL(platform_, SaveStabilizationConfig(_))
      .Times(::testing::AnyNumber());
  HandleAutoDriveCompletion(ad, &stab_mgr_, imu_calib_, platform_);
  EXPECT_NEAR(stab_mgr_.GetConfig().filter.motor_speed_gain, 9.12f, 1e-3f);
}

TEST_F(HandleAutoDriveTest, SpeedCalCompleted_TargetInsideDeadzone_KeepsGain) {
  AutoDriveOutput ad;
  ad.speed_cal_completed = true;
  ad.speed_cal_result.valid = true;
  ad.speed_cal_result.target_throttle = 0.05f;  // внутри деадзоны
  ad.speed_cal_result.mean_speed_ms = 1.0f;
  auto cfg = stab_mgr_.GetConfig();
  cfg.filter.motor_deadzone = 0.05f;
  const float prev_gain = cfg.filter.motor_speed_gain;
  stab_mgr_.SetConfig(cfg, false);
  EXPECT_CALL(platform_, Log(LogLevel::Warning, _)).Times(1);
  HandleAutoDriveCompletion(ad, &stab_mgr_, imu_calib_, platform_);
  EXPECT_FLOAT_EQ(stab_mgr_.GetConfig().filter.motor_speed_gain, prev_gain);
}

// ═══════════════════════════════════════════════════════════════════════════
// BuildSelfTestInput
// ═══════════════════════════════════════════════════════════════════════════

TEST(BuildSelfTestInputTest, NullHandlers_DefaultValues) {
  std::atomic<uint32_t> hz{500};
  MadgwickFilter madgwick;
  VehicleEkf ekf;
  ImuCalibration calib;

  SelfTestContext ctx{hz,      nullptr, madgwick, ekf,   nullptr,
                      nullptr, calib,   nullptr,  false, false};
  auto input = BuildSelfTestInput(ctx);

  EXPECT_EQ(input.loop_hz, 500u);
  EXPECT_FALSE(input.imu_enabled);
  EXPECT_TRUE(input.failsafe_active);  // no rc, no wifi
  EXPECT_EQ(input.log_capacity, 0u);
  EXPECT_EQ(input.pwm_status, -1);  // platform_exists=false
}

TEST(BuildSelfTestInputTest, PlatformExistsAndInited_PwmOk) {
  std::atomic<uint32_t> hz{0};
  MadgwickFilter madgwick;
  VehicleEkf ekf;
  ImuCalibration calib;

  SelfTestContext ctx{hz,      nullptr, madgwick, ekf,  nullptr,
                      nullptr, calib,   nullptr,  true, true};
  auto input = BuildSelfTestInput(ctx);
  EXPECT_EQ(input.pwm_status, 0);
}

TEST(BuildSelfTestInputTest, LoopHzReflected) {
  std::atomic<uint32_t> hz{498};
  MadgwickFilter madgwick;
  VehicleEkf ekf;
  ImuCalibration calib;

  SelfTestContext ctx{hz,      nullptr, madgwick, ekf,  nullptr,
                      nullptr, calib,   nullptr,  true, true};
  EXPECT_EQ(BuildSelfTestInput(ctx).loop_hz, 498u);
}

TEST(BuildSelfTestInputTest, TelemMgrCapacityReflected) {
  std::atomic<uint32_t> hz{0};
  MadgwickFilter madgwick;
  VehicleEkf ekf;
  ImuCalibration calib;
  TelemetryManager telem;
  telem.Init(1000);

  SelfTestContext ctx{hz,      nullptr, madgwick, ekf,  nullptr,
                      nullptr, calib,   &telem,   true, true};
  EXPECT_EQ(BuildSelfTestInput(ctx).log_capacity, 1000u);
}

TEST(BuildSelfTestInputTest, CalibValidReflected) {
  std::atomic<uint32_t> hz{0};
  MadgwickFilter madgwick;
  VehicleEkf ekf;
  ImuCalibration calib;
  // Mark calibration as valid
  ImuCalibData data{};
  data.valid = true;
  calib.SetData(data);

  SelfTestContext ctx{hz,      nullptr, madgwick, ekf,  nullptr,
                      nullptr, calib,   nullptr,  true, true};
  EXPECT_TRUE(BuildSelfTestInput(ctx).calib_valid);
}

// ═══════════════════════════════════════════════════════════════════════════
// ImuHandler::GetRelativeHeadingDeg
// ═══════════════════════════════════════════════════════════════════════════

// FakePlatform subclass that returns controllable mag data
class FakePlatformWithMag : public FakePlatform {
 public:
  void SetMagData(MagData data) { mag_data_ = data; }
  std::optional<MagData> ReadMag() override { return mag_data_; }

 private:
  std::optional<MagData> mag_data_;
};

class ImuHandlerRelHeadingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Provide valid IMU data so Update() doesn't bail early
    platform_.SetImuData(ImuData{0.f, 0.f, 1.f, 0.f, 0.f, 0.f});
    // Provide mag data that produces a predictable heading (flat, pointing N)
    // mx>0, my=0 → atan2(-my, mx) = atan2(0, positive) = 0° → heading = 0°
    platform_.SetMagData(MagData{1000.f, 0.f, 0.f});
    handler_ = std::make_unique<ImuHandler>(platform_, calib_, filter_);
    handler_->SetEnabled(true);
  }

  FakePlatformWithMag platform_;
  ImuCalibration calib_;
  MadgwickFilter filter_;
  std::unique_ptr<ImuHandler> handler_;
};

TEST_F(ImuHandlerRelHeadingTest, InitialRelHeading_IsZero) {
  // Before any Update() with mag, relative heading should be 0
  EXPECT_FLOAT_EQ(handler_->GetRelativeHeadingDeg(), 0.f);
}

TEST_F(ImuHandlerRelHeadingTest, AfterFirstUpdate_RelHeadingIsZero) {
  // After first update, ref is set to current heading → delta = 0
  handler_->Update(2, 2);
  EXPECT_NEAR(handler_->GetRelativeHeadingDeg(), 0.f, 1.f);
}

TEST_F(ImuHandlerRelHeadingTest, ResetHeadingRef_ResetsOnNextUpdate) {
  handler_->Update(2, 2);
  handler_->ResetHeadingRef();
  handler_->Update(4, 2);
  // Still same mag data → same heading → delta still 0
  EXPECT_NEAR(handler_->GetRelativeHeadingDeg(), 0.f, 1.f);
}

// Pure math tests for the wrap-around logic (independent of ImuHandler Update)
TEST(RelativeHeadingMathTest, WrapAround_PositiveDelta_Over180) {
  // Δ = 350 → should wrap to -10
  float delta = 350.f - 0.f;
  if (delta > 180.f) delta -= 360.f;
  if (delta <= -180.f) delta += 360.f;
  EXPECT_NEAR(delta, -10.f, 1e-4f);
}

TEST(RelativeHeadingMathTest, WrapAround_NegativeDelta_Under180) {
  // Δ = -190 → should wrap to +170
  float delta = 170.f - 360.f;
  if (delta > 180.f) delta -= 360.f;
  if (delta <= -180.f) delta += 360.f;
  EXPECT_NEAR(delta, 170.f, 1e-4f);
}

TEST(RelativeHeadingMathTest, WrapAround_Exactly180) {
  // Δ = 180 → stays 180 (not wrapped; condition is > 180)
  float delta = 180.f;
  if (delta > 180.f) delta -= 360.f;
  if (delta <= -180.f) delta += 360.f;
  EXPECT_FLOAT_EQ(delta, 180.f);
}

TEST(RelativeHeadingMathTest, WrapAround_ExactlyMinus180) {
  // Δ = -180 → wraps to +180
  float delta = -180.f;
  if (delta > 180.f) delta -= 360.f;
  if (delta <= -180.f) delta += 360.f;
  EXPECT_FLOAT_EQ(delta, 180.f);
}

TEST(RelativeHeadingMathTest, NoDelta_Zero) {
  float delta = 45.f - 45.f;
  if (delta > 180.f) delta -= 360.f;
  if (delta <= -180.f) delta += 360.f;
  EXPECT_FLOAT_EQ(delta, 0.f);
}
