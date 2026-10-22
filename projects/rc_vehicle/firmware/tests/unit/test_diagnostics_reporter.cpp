#include <gtest/gtest.h>

#include "diagnostics_reporter.hpp"
#include "mock_platform.hpp"
#include "stabilization_manager.hpp"

using namespace rc_vehicle;
using namespace rc_vehicle::testing;

// ══════════════════════════════════════════════════════════════════════════
// MaybePublishDiagnostics — интервал-гейт + сборка снимка
// ══════════════════════════════════════════════════════════════════════════

class MaybePublishDiagnosticsTest : public ::testing::Test {
 protected:
  DiagnosticsContext MakeCtx(const ImuHandler* imu_handler) {
    return DiagnosticsContext{platform_, stab_mgr_,   madgwick_,
                              ekf_,      imu_handler, last_loop_hz_};
  }

  FakePlatform platform_;
  MadgwickFilter madgwick_;
  ImuCalibration imu_calib_;
  VehicleEkf ekf_;
  YawRateController yaw_ctrl_;
  SlipAngleController slip_ctrl_;
  StabilizationManager stab_mgr_{platform_, madgwick_, yaw_ctrl_, slip_ctrl_,
                                 nullptr};
  ImuHandler imu_handler_{platform_, imu_calib_, madgwick_};
  std::atomic<uint32_t> last_loop_hz_{0};

  StabilizationConfig cfg_{};
  uint32_t diag_loop_count_{0};
  uint32_t diag_start_ms_{0};
};

TEST_F(MaybePublishDiagnosticsTest, DoesNotPublish_BeforeInterval) {
  diag_loop_count_ = 5;
  MaybePublishDiagnostics(MakeCtx(nullptr), cfg_, /*now_ms=*/1000,
                          diag_loop_count_, diag_start_ms_);

  EXPECT_EQ(platform_.GetDiagPublishCount(), 0);
  EXPECT_EQ(diag_loop_count_, 5u);  // не сброшен — интервал не сработал
}

TEST_F(MaybePublishDiagnosticsTest, PublishesAtInterval_AndResetsCounters) {
  diag_loop_count_ = 2500;
  MaybePublishDiagnostics(MakeCtx(nullptr), cfg_, /*now_ms=*/5000,
                          diag_loop_count_, diag_start_ms_);

  EXPECT_EQ(platform_.GetDiagPublishCount(), 1);
  EXPECT_EQ(diag_loop_count_, 0u);
  EXPECT_EQ(diag_start_ms_, 5000u);
  EXPECT_EQ(last_loop_hz_.load(), 500u);  // 2500 * 1000 / 5000
  EXPECT_EQ(platform_.GetLastDiagSnap().loop_hz, 500u);
}

TEST_F(MaybePublishDiagnosticsTest, SnapshotReflectsStabConfig) {
  cfg_.enabled = true;
  diag_loop_count_ = 1000;
  MaybePublishDiagnostics(MakeCtx(nullptr), cfg_, /*now_ms=*/5000,
                          diag_loop_count_, diag_start_ms_);

  const auto& snap = platform_.GetLastDiagSnap();
  EXPECT_TRUE(snap.stab_enabled);
  EXPECT_FLOAT_EQ(snap.stab_weight, stab_mgr_.GetStabilizationWeight());
}

TEST_F(MaybePublishDiagnosticsTest, ImuDisabled_LeavesImuFieldsAtDefault) {
  imu_handler_.SetEnabled(false);
  MaybePublishDiagnostics(MakeCtx(&imu_handler_), cfg_, /*now_ms=*/5000,
                          diag_loop_count_, diag_start_ms_);

  const auto& snap = platform_.GetLastDiagSnap();
  EXPECT_FALSE(snap.imu_enabled);
  EXPECT_FLOAT_EQ(snap.pitch_deg, 0.0f);
  EXPECT_FLOAT_EQ(snap.roll_deg, 0.0f);
  EXPECT_FLOAT_EQ(snap.yaw_deg, 0.0f);
  EXPECT_FLOAT_EQ(snap.ekf_vx, 0.0f);
  EXPECT_FLOAT_EQ(snap.ekf_vy, 0.0f);
  EXPECT_FLOAT_EQ(snap.ekf_slip_deg, 0.0f);
}

TEST_F(MaybePublishDiagnosticsTest, ImuEnabled_MirrorsLiveGetters) {
  // Увести Madgwick/EKF от identity-состояния известными значениями, чтобы
  // проверка pass-through была небессмысленной (0 == 0 ничего бы не доказал).
  for (int i = 0; i < 20; ++i) {
    madgwick_.Update(0.5f, 0.3f, 0.8f, 0.0f, 0.0f, 0.0f, 0.01f);
  }
  ekf_.SetState(1.23f, -0.45f, 0.1f);
  imu_handler_.SetEnabled(true);
  // Платформа без IMU-данных: два тика чтения → два неудачных чтения.
  imu_handler_.Update(2, 2);
  imu_handler_.Update(4, 2);

  float exp_pitch = 0.0f, exp_roll = 0.0f, exp_yaw = 0.0f;
  madgwick_.GetEulerDeg(exp_pitch, exp_roll, exp_yaw);

  MaybePublishDiagnostics(MakeCtx(&imu_handler_), cfg_, /*now_ms=*/5000,
                          diag_loop_count_, diag_start_ms_);

  const auto& snap = platform_.GetLastDiagSnap();
  EXPECT_TRUE(snap.imu_enabled);
  EXPECT_FLOAT_EQ(snap.pitch_deg, exp_pitch);
  EXPECT_FLOAT_EQ(snap.roll_deg, exp_roll);
  EXPECT_FLOAT_EQ(snap.yaw_deg, exp_yaw);
  EXPECT_FLOAT_EQ(snap.filtered_gz, imu_handler_.GetFilteredGyroZ());
  EXPECT_FLOAT_EQ(snap.ekf_vx, ekf_.GetVx());
  EXPECT_FLOAT_EQ(snap.ekf_vy, ekf_.GetVy());
  EXPECT_FLOAT_EQ(snap.ekf_slip_deg, ekf_.GetSlipAngleDeg());
  EXPECT_EQ(snap.imu_read_stats.failed_reads, 2u);
  EXPECT_EQ(snap.imu_read_stats.max_consecutive_misses, 2u);
}

// ══════════════════════════════════════════════════════════════════════════
// EmitDiagnostics — форматирование снимка (чистая функция, как BuildTelemJson)
// ══════════════════════════════════════════════════════════════════════════

TEST(EmitDiagnosticsTest, LogsCoreLoadAndDiagLine_NoImuBlocks) {
  FakePlatform platform;
  DiagnosticsSnapshot snap{};
  snap.loop_hz = 250;
  snap.stab_enabled = false;
  snap.stab_weight = 0.0f;
  snap.imu_enabled = false;

  EmitDiagnostics(platform, snap);

  EXPECT_EQ(platform.GetLogCoreLoadCount(), 1);
  const auto& msgs = platform.GetLoggedMessages();
  ASSERT_EQ(msgs.size(), 1u);
  EXPECT_EQ(msgs[0], "DIAG: loop=250 Hz  stab=OFF (w=0.00)");
}

TEST(EmitDiagnosticsTest, LogsImuAndEkfLines_WhenImuEnabled) {
  FakePlatform platform;
  DiagnosticsSnapshot snap{};
  snap.loop_hz = 480;
  snap.stab_enabled = true;
  snap.stab_weight = 0.5f;
  snap.imu_enabled = true;
  snap.pitch_deg = 5.5f;
  snap.roll_deg = -2.5f;
  snap.yaw_deg = 180.0f;
  snap.filtered_gz = 0.1f;
  snap.ekf_vx = 1.23f;
  snap.ekf_vy = -0.45f;
  snap.ekf_slip_deg = 3.1f;

  EmitDiagnostics(platform, snap);

  EXPECT_EQ(platform.GetLogCoreLoadCount(), 1);
  const auto& msgs = platform.GetLoggedMessages();
  ASSERT_EQ(msgs.size(), 4u);
  EXPECT_EQ(msgs[0], "DIAG: loop=480 Hz  stab=ON (w=0.50)");
  EXPECT_EQ(msgs[1], "IMU: P=5.5 R=-2.5 Y=180.0 deg  gz=0.1 dps");
  EXPECT_EQ(msgs[2], "EKF: vx=1.23 vy=-0.45 m/s  slip=3.1 deg");
  EXPECT_EQ(msgs[3], "IMU err: read=0 nan=0 who=0 run=0");
}

// LOS-383: счётчики ошибок чтения IMU — целые, без влияния std::fixed.
TEST(EmitDiagnosticsTest, LogsImuReadErrorCounters) {
  FakePlatform platform;
  DiagnosticsSnapshot snap{};
  snap.imu_enabled = true;
  snap.imu_read_stats.failed_reads = 12;
  snap.imu_read_stats.invalid_samples = 3;
  snap.imu_read_stats.whoami_mismatches = 1;
  snap.imu_read_stats.max_consecutive_misses = 7;

  EmitDiagnostics(platform, snap);

  const auto& msgs = platform.GetLoggedMessages();
  ASSERT_EQ(msgs.size(), 4u);
  EXPECT_EQ(msgs[3], "IMU err: read=12 nan=3 who=1 run=7");
}
