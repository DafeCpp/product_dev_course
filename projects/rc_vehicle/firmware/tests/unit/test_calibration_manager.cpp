#include <gtest/gtest.h>

#include "calibration_manager.hpp"
#include "mock_platform.hpp"
#include "vehicle_ekf.hpp"

using namespace rc_vehicle;
using namespace rc_vehicle::testing;

// ═══════════════════════════════════════════════════════════════════════════
// Test fixture
// ═══════════════════════════════════════════════════════════════════════════

class CalibrationManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    mgr_ = std::make_unique<CalibrationManager>(platform_, imu_calib_,
                                                madgwick_, &ekf_);
  }

  FakePlatform platform_;
  ImuCalibration imu_calib_;
  MadgwickFilter madgwick_;
  VehicleEkf ekf_;
  std::unique_ptr<CalibrationManager> mgr_;
};

// ═══════════════════════════════════════════════════════════════════════════
// Status
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(CalibrationManagerTest, InitialStatus_IsIdle) {
  EXPECT_STREQ(mgr_->GetStatus(), "idle");
  EXPECT_EQ(mgr_->GetStage(), 0);
}

TEST_F(CalibrationManagerTest, IsAutoForwardActive_InitiallyFalse) {
  EXPECT_FALSE(mgr_->IsAutoForwardActive());
}

// ═══════════════════════════════════════════════════════════════════════════
// StartCalibration + ProcessRequest
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(CalibrationManagerTest, StartCalibration_GyroOnly_StartsCollecting) {
  mgr_->StartCalibration(false);  // gyro only
  mgr_->ProcessRequest(0);

  EXPECT_STREQ(mgr_->GetStatus(), "collecting");
}

TEST_F(CalibrationManagerTest, StartCalibration_Full_StartsCollecting) {
  mgr_->StartCalibration(true);  // full
  mgr_->ProcessRequest(0);

  EXPECT_STREQ(mgr_->GetStatus(), "collecting");
}

TEST_F(CalibrationManagerTest, ProcessRequest_NoRequest_DoesNothing) {
  // No StartCalibration() call
  mgr_->ProcessRequest(0);
  EXPECT_STREQ(mgr_->GetStatus(), "idle");
}

// ═══════════════════════════════════════════════════════════════════════════
// ProcessCompletion
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(CalibrationManagerTest, ProcessCompletion_NoStatusChange_DoesNothing) {
  // Status stays idle → no action
  mgr_->ProcessCompletion(0);
  EXPECT_STREQ(mgr_->GetStatus(), "idle");
}

// Код-ревью PR #290 (5-й раунд, LOS-240): завершение Full/Forward
// калибровки меняет базис RotateToVehicleFrame() (через SetVehicleFrame()
// Madgwick + новые gravity_vec/accel_forward_vec), но TiltEstimator —
// член ControlLoopProcessor, не CalibrationManager, и не сбрасывался.
// ConsumeFrameChanged() — сигнал вызывающему коду сделать это сам.
TEST_F(CalibrationManagerTest, ProcessCompletion_Done_SetsFrameChanged) {
  imu_calib_.StartCalibration(CalibMode::Full, 10);
  for (int i = 0; i < 10; ++i) {
    ImuData d{};
    d.az = 1.0f;
    imu_calib_.FeedSample(d);
  }
  ASSERT_EQ(imu_calib_.GetStatus(), CalibStatus::Done);

  EXPECT_FALSE(mgr_->ConsumeFrameChanged())
      << "флаг не должен быть выставлен до ProcessCompletion()";

  mgr_->ProcessCompletion(0);
  EXPECT_TRUE(mgr_->ConsumeFrameChanged());
  EXPECT_FALSE(mgr_->ConsumeFrameChanged())
      << "повторный вызов должен вернуть false — флаг одноразовый";
}

// Код-ревью PR #290 (6-й раунд): SetForwardDirection() — ещё один путь
// смены базиса RotateToVehicleFrame() (ручная WS-команда), помимо
// ProcessCompletion(). Раньше он не обновлял ни Madgwick vehicle frame,
// ни frame_changed_.
//
// 7-й раунд: SetForwardDirection() вызывается с потока WS-сервера и теперь
// только откладывает запрос (imu_calib_/madgwick_ не потокобезопасны) —
// эффект применяется ProcessForwardDirectionRequest() на потоке control
// loop, как и здесь в тесте.
TEST_F(CalibrationManagerTest, SetForwardDirection_SetsFrameChanged) {
  ImuCalibData d{};
  d.valid = true;
  imu_calib_.SetData(d);

  EXPECT_FALSE(mgr_->ConsumeFrameChanged());
  mgr_->SetForwardDirection(0.f, 1.f, 0.f);
  EXPECT_FALSE(mgr_->ConsumeFrameChanged())
      << "SetForwardDirection() откладывает запрос — эффекта быть не должно "
         "до ProcessForwardDirectionRequest()";
  mgr_->ProcessForwardDirectionRequest();
  EXPECT_TRUE(mgr_->ConsumeFrameChanged());
  EXPECT_FALSE(mgr_->ConsumeFrameChanged())
      << "повторный вызов должен вернуть false — флаг одноразовый";
}

TEST_F(CalibrationManagerTest,
       ProcessForwardDirectionRequest_NoPendingRequest_DoesNothing) {
  mgr_->ProcessForwardDirectionRequest();
  EXPECT_FALSE(mgr_->ConsumeFrameChanged());
}

TEST_F(CalibrationManagerTest,
       ProcessCompletion_Failed_DoesNotSetFrameChanged) {
  imu_calib_.StartCalibration(CalibMode::Full, 10);
  // Гироскоп «шумит» — калибровка не должна пройти (variance > порога).
  for (int i = 0; i < 10; ++i) {
    ImuData d{};
    d.gz = (i % 2 == 0) ? 10.0f : -10.0f;
    d.az = 1.0f;
    imu_calib_.FeedSample(d);
  }
  ASSERT_EQ(imu_calib_.GetStatus(), CalibStatus::Failed);

  mgr_->ProcessCompletion(0);
  EXPECT_FALSE(mgr_->ConsumeFrameChanged());
}

// ═══════════════════════════════════════════════════════════════════════════
// LoadFromNvs
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(CalibrationManagerTest, LoadFromNvs_NoData_ReturnsFalse) {
  EXPECT_FALSE(mgr_->LoadFromNvs());
}

TEST_F(CalibrationManagerTest, LoadFromNvs_WithData_ReturnsTrue) {
  ImuCalibData data{};
  data.gyro_bias[0] = 0.1f;
  data.gyro_bias[1] = 0.2f;
  data.gyro_bias[2] = 0.3f;
  platform_.SetCalibData(data);

  EXPECT_TRUE(mgr_->LoadFromNvs());
}

// ═══════════════════════════════════════════════════════════════════════════
// AutoForward
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(CalibrationManagerTest, StartAutoForward_RequiresStage1) {
  // Without stage 1 calibration done, forward calib should fail
  EXPECT_FALSE(mgr_->StartAutoForwardCalibration(0.1f));
  EXPECT_FALSE(mgr_->IsAutoForwardActive());
}

TEST_F(CalibrationManagerTest, StopAutoForward_WhenNotActive_DoesNothing) {
  EXPECT_FALSE(mgr_->IsAutoForwardActive());
  mgr_->StopAutoForward();  // Should not crash
  EXPECT_FALSE(mgr_->IsAutoForwardActive());
}

// Досрочная остановка авто-движения обязана прекратить и сбор семплов.
// Иначе этап 2 доберёт остаток без управляемого разгона и запишет мусорную
// ось «вперёд» (замечание code review к LOS-214).
TEST_F(CalibrationManagerTest, StopAutoForward_CancelsSampleCollection) {
  // Стадия 1 (Full) — предусловие для forward-калибровки
  ImuCalibData d{};
  d.valid = true;
  imu_calib_.SetData(d);

  ASSERT_TRUE(mgr_->StartAutoForwardCalibration(0.1f));
  ASSERT_TRUE(mgr_->IsAutoForwardActive());
  ASSERT_EQ(imu_calib_.GetStatus(), CalibStatus::Collecting);

  mgr_->StopAutoForward();

  EXPECT_FALSE(mgr_->IsAutoForwardActive());
  EXPECT_EQ(imu_calib_.GetStatus(), CalibStatus::Failed)
      << "сбор семплов продолжается после остановки движения";
}

// ═══════════════════════════════════════════════════════════════════════════
// События auto-forward калибровки (LOS-226)
// ═══════════════════════════════════════════════════════════════════════════

/** Подготовить стадию 1 и подключить лог событий. */
static void PrepareStage1(ImuCalibration& calib) {
  ImuCalibData d{};
  d.valid = true;
  calib.SetData(d);
}

TEST_F(CalibrationManagerTest, AutoForwardStart_EventHasPlatformTimestamp) {
  PrepareStage1(imu_calib_);
  TelemetryEventLog log;
  mgr_->SetEventLog(&log);
  platform_.SetTimeMs(54321);

  ASSERT_TRUE(mgr_->StartAutoForwardCalibration(0.1f));

  TelemetryEvent ev{};
  ASSERT_TRUE(log.GetEvent(log.Count() - 1, ev));
  EXPECT_EQ(ev.type, TelemetryEventType::ImuCalibStart);
  EXPECT_EQ(ev.param, 2);  // stage 2 = auto_forward
  EXPECT_EQ(ev.ts_ms, 54321u) << "событие старта без метки времени";
}

// До LOS-226 обрыв auto-forward не оставлял в логе никакого следа, хотя после
// LOS-214 это штатный путь (аборт по перехвату пультом).
TEST_F(CalibrationManagerTest, StopAutoForward_LogsAbortEvent) {
  PrepareStage1(imu_calib_);
  TelemetryEventLog log;
  mgr_->SetEventLog(&log);
  platform_.SetTimeMs(1000);
  ASSERT_TRUE(mgr_->StartAutoForwardCalibration(0.1f));

  platform_.SetTimeMs(2500);
  mgr_->StopAutoForward();

  TelemetryEvent ev{};
  ASSERT_TRUE(log.GetEvent(log.Count() - 1, ev));
  EXPECT_EQ(ev.type, TelemetryEventType::ImuCalibFailed);
  EXPECT_EQ(ev.param, 2);
  EXPECT_EQ(ev.ts_ms, 2500u);
}

// ProcessCompletion() выполняется РАНЬШЕ UpdateAutoDrive() в том же тике
// (control_loop_processor.cpp:50 против :54), поэтому обрыв фиксируется здесь,
// а на следующем тике ProcessCompletion() видит переход Collecting → Failed.
// Без подавления получилось бы два ImuCalibFailed на один обрыв, причём второй
// с param = 0: после отмены GetCalibStage() уже не возвращает стадию.
TEST_F(CalibrationManagerTest, StopAutoForward_AbortLoggedOnce) {
  PrepareStage1(imu_calib_);
  TelemetryEventLog log;
  mgr_->SetEventLog(&log);
  ASSERT_TRUE(mgr_->StartAutoForwardCalibration(0.1f));

  mgr_->StopAutoForward();
  mgr_->ProcessCompletion(100);  // следующий тик control loop

  int failed = 0;
  for (size_t i = 0; i < log.Count(); ++i) {
    TelemetryEvent ev{};
    ASSERT_TRUE(log.GetEvent(i, ev));
    if (ev.type == TelemetryEventType::ImuCalibFailed) {
      ++failed;
      EXPECT_EQ(ev.param, 2) << "стадия обрыва потеряна";
    }
  }
  EXPECT_EQ(failed, 1) << "обрыв записан " << failed << " раз(а)";
}

// ProcessCompletion() зовёт StopAutoForward() и при УСПЕШНОМ завершении —
// событие обрыва там было бы ложным.
TEST_F(CalibrationManagerTest, StopAutoForward_AfterDone_NoFalseAbortEvent) {
  PrepareStage1(imu_calib_);
  TelemetryEventLog log;
  mgr_->SetEventLog(&log);
  ASSERT_TRUE(mgr_->StartAutoForwardCalibration(0.1f));

  // Сбор завершился штатно
  imu_calib_.StartForwardCalibration(1);
  imu_calib_.FeedSample([] {
    ImuData d{};
    d.ay = 0.3f;
    d.az = 1.0f;
    return d;
  }());
  ASSERT_NE(imu_calib_.GetStatus(), CalibStatus::Collecting);

  const size_t before = log.Count();
  mgr_->StopAutoForward();

  for (size_t i = before; i < log.Count(); ++i) {
    TelemetryEvent ev{};
    ASSERT_TRUE(log.GetEvent(i, ev));
    EXPECT_NE(ev.type, TelemetryEventType::ImuCalibFailed)
        << "ложное событие обрыва после успешной калибровки";
  }
}

TEST_F(CalibrationManagerTest, UpdateAutoForward_WhenNotActive_ReturnsZero) {
  float throttle = mgr_->UpdateAutoForward(0.0f, 1.0f, 0.0f, 0.002f);
  EXPECT_FLOAT_EQ(throttle, 0.0f);
}

TEST_F(CalibrationManagerTest, UpdateAutoForward_ZeroDt_ReturnsZero) {
  float throttle = mgr_->UpdateAutoForward(0.0f, 1.0f, 0.0f, 0.0f);
  EXPECT_FLOAT_EQ(throttle, 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// StartAutoCalibration
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(CalibrationManagerTest, StartAutoCalibration_StartsFullCalibration) {
  mgr_->StartAutoCalibration();
  EXPECT_STREQ(mgr_->GetStatus(), "collecting");
}
