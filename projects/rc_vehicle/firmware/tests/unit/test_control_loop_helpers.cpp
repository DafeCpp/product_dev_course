#include <atomic>
#include <cmath>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "control_loop_helpers.hpp"
#include "imu_calibration.hpp"
#include "mock_platform.hpp"
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
  EXPECT_FLOAT_EQ(thr, 0.8f);
  EXPECT_FLOAT_EQ(steer, -0.4f);
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

TEST(SelectControlSourceTest, RcPriorityOverWifi) {
  SensorSnapshot s;
  s.rc_active = true;
  s.rc_cmd = RcCommand{1.0f, 0.0f};
  s.wifi_active = true;
  s.wifi_cmd = RcCommand{-1.0f, -1.0f};
  float thr = 0.0f, steer = 0.0f;
  EXPECT_TRUE(SelectControlSource(s, thr, steer));
  EXPECT_FLOAT_EQ(thr, 1.0f);   // RC wins
  EXPECT_FLOAT_EQ(steer, 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// BuildAutoDriveInput
// ═══════════════════════════════════════════════════════════════════════════

TEST(BuildAutoDriveInputTest, ImuDisabled_ZeroAccel) {
  SensorSnapshot s;
  s.imu_enabled = false;
  s.rc_active = true;
  ImuCalibration calib;

  auto ad = BuildAutoDriveInput(s, calib, 2);
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
  ImuCalibration calib;

  auto ad = BuildAutoDriveInput(s, calib, 4);
  EXPECT_TRUE(ad.imu_enabled);
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
  ImuCalibration calib;

  auto ad = BuildAutoDriveInput(s, calib, 2);
  EXPECT_NEAR(ad.accel_mag, std::sqrt(3.0f), 1e-5f);
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
  EXPECT_CALL(platform_, SaveStabilizationConfig(_)).Times(::testing::AnyNumber());
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

// ═══════════════════════════════════════════════════════════════════════════
// BuildSelfTestInput
// ═══════════════════════════════════════════════════════════════════════════

TEST(BuildSelfTestInputTest, NullHandlers_DefaultValues) {
  std::atomic<uint32_t> hz{500};
  MadgwickFilter madgwick;
  VehicleEkf ekf;
  ImuCalibration calib;

  SelfTestContext ctx{hz, nullptr, madgwick, ekf,
                      nullptr, nullptr, calib, nullptr, false, false};
  auto input = BuildSelfTestInput(ctx);

  EXPECT_EQ(input.loop_hz, 500u);
  EXPECT_FALSE(input.imu_enabled);
  EXPECT_TRUE(input.failsafe_active);  // no rc, no wifi
  EXPECT_EQ(input.log_capacity, 0u);
  EXPECT_EQ(input.pwm_status, -1);    // platform_exists=false
}

TEST(BuildSelfTestInputTest, PlatformExistsAndInited_PwmOk) {
  std::atomic<uint32_t> hz{0};
  MadgwickFilter madgwick;
  VehicleEkf ekf;
  ImuCalibration calib;

  SelfTestContext ctx{hz, nullptr, madgwick, ekf,
                      nullptr, nullptr, calib, nullptr, true, true};
  auto input = BuildSelfTestInput(ctx);
  EXPECT_EQ(input.pwm_status, 0);
}

TEST(BuildSelfTestInputTest, LoopHzReflected) {
  std::atomic<uint32_t> hz{498};
  MadgwickFilter madgwick;
  VehicleEkf ekf;
  ImuCalibration calib;

  SelfTestContext ctx{hz, nullptr, madgwick, ekf,
                      nullptr, nullptr, calib, nullptr, true, true};
  EXPECT_EQ(BuildSelfTestInput(ctx).loop_hz, 498u);
}

TEST(BuildSelfTestInputTest, TelemMgrCapacityReflected) {
  std::atomic<uint32_t> hz{0};
  MadgwickFilter madgwick;
  VehicleEkf ekf;
  ImuCalibration calib;
  TelemetryManager telem;
  telem.Init(1000);

  SelfTestContext ctx{hz, nullptr, madgwick, ekf,
                      nullptr, nullptr, calib, &telem, true, true};
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

  SelfTestContext ctx{hz, nullptr, madgwick, ekf,
                      nullptr, nullptr, calib, nullptr, true, true};
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
  if (delta >  180.f) delta -= 360.f;
  if (delta <= -180.f) delta += 360.f;
  EXPECT_NEAR(delta, -10.f, 1e-4f);
}

TEST(RelativeHeadingMathTest, WrapAround_NegativeDelta_Under180) {
  // Δ = -190 → should wrap to +170
  float delta = 170.f - 360.f;
  if (delta >  180.f) delta -= 360.f;
  if (delta <= -180.f) delta += 360.f;
  EXPECT_NEAR(delta, 170.f, 1e-4f);
}

TEST(RelativeHeadingMathTest, WrapAround_Exactly180) {
  // Δ = 180 → stays 180 (not wrapped; condition is > 180)
  float delta = 180.f;
  if (delta >  180.f) delta -= 360.f;
  if (delta <= -180.f) delta += 360.f;
  EXPECT_FLOAT_EQ(delta, 180.f);
}

TEST(RelativeHeadingMathTest, WrapAround_ExactlyMinus180) {
  // Δ = -180 → wraps to +180
  float delta = -180.f;
  if (delta >  180.f) delta -= 360.f;
  if (delta <= -180.f) delta += 360.f;
  EXPECT_FLOAT_EQ(delta, 180.f);
}

TEST(RelativeHeadingMathTest, NoDelta_Zero) {
  float delta = 45.f - 45.f;
  if (delta >  180.f) delta -= 360.f;
  if (delta <= -180.f) delta += 360.f;
  EXPECT_FLOAT_EQ(delta, 0.f);
}
