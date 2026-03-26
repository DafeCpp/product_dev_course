#include <gtest/gtest.h>

#include "calibration_manager.hpp"
#include "control_loop_processor.hpp"
#include "mock_platform.hpp"
#include "stabilization_manager.hpp"
#include "telemetry_manager.hpp"

using namespace rc_vehicle;
using namespace rc_vehicle::testing;
using rc_vehicle::BrakingMode;

// ═══════════════════════════════════════════════════════════════════════════
// Fixture
// ═══════════════════════════════════════════════════════════════════════════

class ProcessorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    stab_mgr_ = std::make_unique<StabilizationManager>(
        platform_, madgwick_, yaw_ctrl_, slip_ctrl_, nullptr);
    calib_mgr_ = std::make_unique<CalibrationManager>(
        platform_, imu_calib_, madgwick_, &ekf_);
    wifi_handler_ = std::make_unique<WifiCommandHandler>(
        platform_, /*timeout_ms=*/500);
    telem_mgr_ = std::make_unique<TelemetryManager>();
    telem_mgr_->Init(1000);

    auto_drive_.SetCalibrationManager(calib_mgr_.get());

    ctx_ = std::make_unique<ControlLoopContext>(ControlLoopContext{
        platform_,        imu_calib_,        madgwick_,       ekf_,
        yaw_ctrl_,        pitch_ctrl_,        slip_ctrl_,      oversteer_guard_,
        kids_processor_,  auto_drive_,
        calib_mgr_.get(), stab_mgr_.get(),    telem_mgr_.get(),
        nullptr,          wifi_handler_.get(), nullptr, nullptr,
        last_loop_hz_});

    processor_ = std::make_unique<ControlLoopProcessor>(*ctx_, 0);
  }

  /** Шаг с автоматическим продвижением времени (dt = 2 ms). */
  void Step() {
    time_ms_ += 2;
    processor_->Step(time_ms_, 2);
  }

  void RunSteps(uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) Step();
  }

  /** Переключить в DirectLaw (use_slew_rate=false, PWM без задержки). */
  void SetDirectLaw() {
    auto cfg = stab_mgr_->GetConfig();
    cfg.mode = DriveMode::DirectLaw;
    stab_mgr_->SetConfig(cfg);
  }

  FakePlatform platform_;
  ImuCalibration imu_calib_;
  MadgwickFilter madgwick_;
  VehicleEkf ekf_;
  YawRateController yaw_ctrl_;
  PitchCompensator pitch_ctrl_;
  SlipAngleController slip_ctrl_;
  OversteerGuard oversteer_guard_;
  KidsModeProcessor kids_processor_;
  AutoDriveCoordinator auto_drive_;
  std::atomic<uint32_t> last_loop_hz_{0};

  std::unique_ptr<StabilizationManager> stab_mgr_;
  std::unique_ptr<CalibrationManager> calib_mgr_;
  std::unique_ptr<WifiCommandHandler> wifi_handler_;
  std::unique_ptr<TelemetryManager> telem_mgr_;
  std::unique_ptr<ControlLoopContext> ctx_;
  std::unique_ptr<ControlLoopProcessor> processor_;

  uint32_t time_ms_{0};
};

// ═══════════════════════════════════════════════════════════════════════════
// Базовые инварианты
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(ProcessorTest, SingleStep_NoCrash) {
  EXPECT_NO_THROW(Step());
}

TEST_F(ProcessorTest, MultipleSteps_NoCrash) {
  EXPECT_NO_THROW(RunSteps(50));
}

TEST_F(ProcessorTest, NullHandlers_NoCrash) {
  // Пересобрать с минимальным контекстом (rc/imu/telem handler = null)
  ControlLoopContext minimal_ctx{
      platform_,        imu_calib_,        madgwick_,       ekf_,
      yaw_ctrl_,        pitch_ctrl_,        slip_ctrl_,      oversteer_guard_,
      kids_processor_,  auto_drive_,
      calib_mgr_.get(), stab_mgr_.get(),    nullptr,
      nullptr,          nullptr,            nullptr, nullptr,
      last_loop_hz_};
  ControlLoopProcessor proc(minimal_ctx, 0);
  EXPECT_NO_THROW(proc.Step(2, 2));
}

// ═══════════════════════════════════════════════════════════════════════════
// Failsafe
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(ProcessorTest, NoSignal_FailsafeCallsSetPwmNeutral) {
  // Нет RC, нет Wi-Fi → failsafe активируется → SetPwmNeutral
  int before = platform_.GetPwmSetCount();
  Step();
  EXPECT_GT(platform_.GetPwmSetCount(), before);
  EXPECT_FLOAT_EQ(platform_.GetLastThrottle(), 0.0f);
  EXPECT_FLOAT_EQ(platform_.GetLastSteering(), 0.0f);
}

TEST_F(ProcessorTest, WifiActive_NoFailsafe) {
  SetDirectLaw();
  platform_.SetWifiCommand({0.5f, 0.0f});
  // Первый шаг: handler получает команду, становится активным
  Step();
  // Failsafe не должен сработать — platform записывает SetPwm, не SetPwmNeutral
  // Если failsafe: throttle = 0. При WiFi active: throttle = 0.5 (без slew).
  EXPECT_GT(platform_.GetLastThrottle(), 0.0f);
}

TEST_F(ProcessorTest, Failsafe_ResetsToNeutral) {
  SetDirectLaw();
  // Инжектировать команду, потом убрать → failsafe должен обнулить
  platform_.SetWifiCommand({0.8f, 0.4f});
  Step();  // wifi active
  EXPECT_GT(platform_.GetLastThrottle(), 0.0f);

  // Убрать команду с платформы → handler больше не получает свежих данных
  platform_.ClearWifiCommand();
  // Симулируем истечение таймаута (advance 600ms)
  time_ms_ += 600;
  processor_->Step(time_ms_, 600);
  // WiFi истёк → failsafe → neutral
  EXPECT_FLOAT_EQ(platform_.GetLastThrottle(), 0.0f);
  EXPECT_FLOAT_EQ(platform_.GetLastSteering(), 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// WiFi команда → PWM
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(ProcessorTest, WifiCommand_ReachesSetPwm) {
  SetDirectLaw();
  platform_.SetWifiCommand({0.6f, -0.3f});
  Step();
  EXPECT_NEAR(platform_.GetLastThrottle(), 0.6f, 1e-4f);
  EXPECT_NEAR(platform_.GetLastSteering(), -0.3f, 1e-4f);
}

TEST_F(ProcessorTest, WifiCommand_WithTrim_OffsetApplied) {
  SetDirectLaw();
  auto cfg = stab_mgr_->GetConfig();
  cfg.steering_trim = 0.05f;
  cfg.throttle_trim = 0.0f;
  stab_mgr_->SetConfig(cfg);

  platform_.SetWifiCommand({0.5f, 0.2f});
  Step();
  EXPECT_NEAR(platform_.GetLastSteering(), 0.2f + 0.05f, 1e-4f);
}

TEST_F(ProcessorTest, NoCommand_NeutralPwm_AfterFailsafe) {
  SetDirectLaw();
  Step();  // нет источника управления → failsafe → neutral
  EXPECT_FLOAT_EQ(platform_.GetLastThrottle(), 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Slew rate (Normal mode)
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(ProcessorTest, SlewRate_ThrottleRampsGradually) {
  // Normal mode: slew_throttle по умолчанию 0.5/с
  // За 1 шаг 2ms: max_change = 0.5 * 0.020 = 0.01 (PWM обновляется раз в 20ms)
  platform_.SetWifiCommand({1.0f, 0.0f});
  RunSteps(10);  // 20ms → первое PWM обновление
  float throttle_after_20ms = platform_.GetLastThrottle();
  EXPECT_GT(throttle_after_20ms, 0.0f);
  EXPECT_LT(throttle_after_20ms, 1.0f);  // не достигли полного значения
}

TEST_F(ProcessorTest, SlewRate_EventuallyReachesTarget) {
  platform_.SetWifiCommand({0.5f, 0.0f});
  // 2 секунды = 1000 шагов → успеет дойти при 0.5/с slew
  RunSteps(1000);
  EXPECT_NEAR(platform_.GetLastThrottle(), 0.5f, 0.01f);
}

// ═══════════════════════════════════════════════════════════════════════════
// BrakingMode
// ═══════════════════════════════════════════════════════════════════════════

/** Включить BrakingMode::Brake в Normal mode (slew_throttle=0.5, mult=4 → 2/s) */
// Note: helper defined as free function to avoid name clash with member
static void SetBrakeMode(StabilizationManager& stab_mgr, float multiplier = 4.0f) {
  auto cfg = stab_mgr.GetConfig();
  cfg.braking_mode = BrakingMode::Brake;
  cfg.brake_slew_multiplier = multiplier;
  stab_mgr.SetConfig(cfg);
}

TEST_F(ProcessorTest, BrakeMode_DeceleratesFasterThanCoast) {
  // Разгоняемся до ~0.5, затем посылаем commanded=0 (wifi остаётся активным).
  // В Brake mode decel slew = 0.5 * 4 = 2.0/s → за 250ms достигает 0.

  platform_.SetWifiCommand({0.5f, 0.0f});
  RunSteps(1000);  // 2 сек → дошли до 0.5
  EXPECT_NEAR(platform_.GetLastThrottle(), 0.5f, 0.02f);

  SetBrakeMode(*stab_mgr_);
  // Посылаем commanded=0 (не ClearWifiCommand — failsafe не срабатывает)
  platform_.SetWifiCommand({0.0f, 0.0f});

  // 2.0/s decel → 0.5 / 2.0 = 0.25s = 125 steps по 2ms
  RunSteps(130);
  EXPECT_NEAR(platform_.GetLastThrottle(), 0.0f, 0.05f);
}

TEST_F(ProcessorTest, CoastMode_DeceleratesSlowly) {
  // Coast mode (default): разгон, потом commanded=0 — decel slew=0.5/s
  platform_.SetWifiCommand({0.5f, 0.0f});
  RunSteps(1000);
  EXPECT_NEAR(platform_.GetLastThrottle(), 0.5f, 0.02f);

  platform_.SetWifiCommand({0.0f, 0.0f});

  // 0.5/s → за 250ms (125 шагов) изменится не более 0.125 → applied ≥ 0.35
  RunSteps(125);
  EXPECT_GT(platform_.GetLastThrottle(), 0.2f);  // всё ещё далеко от нуля
}

// ═══════════════════════════════════════════════════════════════════════════
// Телеметрия
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(ProcessorTest, WithImu_TelemLogPopulated) {
  // Добавить ImuHandler чтобы sensors.imu_enabled = true
  ImuHandler imu_handler(platform_, imu_calib_, madgwick_, 2);
  imu_handler.SetEnabled(true);
  ImuData imu_data{};
  imu_data.az = 1.0f;
  platform_.SetImuData(imu_data);

  ctx_->imu_handler = &imu_handler;

  // Запустить 100 шагов (200ms > kLogIntervalMs=10ms → несколько записей)
  platform_.SetWifiCommand({0.0f, 0.0f});
  RunSteps(100);

  size_t count = 0, cap = 0;
  telem_mgr_->GetLogInfo(count, cap);
  EXPECT_GT(count, 0u);
}

TEST_F(ProcessorTest, WithoutImu_TelemLogEmpty) {
  // Без IMU-хендлера imu_enabled=false → лог не пишется
  platform_.SetWifiCommand({0.0f, 0.0f});
  RunSteps(100);

  size_t count = 0, cap = 0;
  telem_mgr_->GetLogInfo(count, cap);
  EXPECT_EQ(count, 0u);
}

// ═══════════════════════════════════════════════════════════════════════════
// CalibrationManager
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(ProcessorTest, CalibMgr_Null_NoCrash) {
  // Пересобрать без calib_mgr
  ControlLoopContext ctx{
      platform_,  imu_calib_, madgwick_,       ekf_,
      yaw_ctrl_,  pitch_ctrl_, slip_ctrl_,      oversteer_guard_,
      kids_processor_, auto_drive_,
      nullptr,    stab_mgr_.get(), telem_mgr_.get(),
      nullptr,    nullptr,         nullptr, nullptr,
      last_loop_hz_};
  ControlLoopProcessor proc(ctx, 0);
  EXPECT_NO_THROW(proc.Step(2, 2));
}
