#include <gtest/gtest.h>

#include "calibration_manager.hpp"
#include "config.hpp"
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
    calib_mgr_ = std::make_unique<CalibrationManager>(platform_, imu_calib_,
                                                      madgwick_, &ekf_);
    wifi_handler_ =
        std::make_unique<WifiCommandHandler>(platform_, /*timeout_ms=*/500);
    telem_mgr_ = std::make_unique<TelemetryManager>();
    telem_mgr_->Init(1000);

    auto_drive_.SetCalibrationManager(calib_mgr_.get());

    ctx_ = std::make_unique<ControlLoopContext>(ControlLoopContext{
        platform_, imu_calib_, madgwick_, ekf_, yaw_ctrl_, pitch_ctrl_,
        slip_ctrl_, oversteer_guard_, kids_processor_, auto_drive_,
        calib_mgr_.get(), stab_mgr_.get(), telem_mgr_.get(), nullptr,
        wifi_handler_.get(), nullptr, nullptr, last_loop_hz_});

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

TEST_F(ProcessorTest, SingleStep_NoCrash) { EXPECT_NO_THROW(Step()); }

TEST_F(ProcessorTest, MultipleSteps_NoCrash) { EXPECT_NO_THROW(RunSteps(50)); }

TEST_F(ProcessorTest, NullHandlers_NoCrash) {
  // Пересобрать с минимальным контекстом (rc/imu/telem handler = null)
  ControlLoopContext minimal_ctx{platform_,   imu_calib_,       madgwick_,
                                 ekf_,        yaw_ctrl_,        pitch_ctrl_,
                                 slip_ctrl_,  oversteer_guard_, kids_processor_,
                                 auto_drive_, calib_mgr_.get(), stab_mgr_.get(),
                                 nullptr,     nullptr,          nullptr,
                                 nullptr,     nullptr,          last_loop_hz_};
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

TEST_F(ProcessorTest, Failsafe_WithTrim_PwmStaysNeutral) {
  // FW-R1: при активном failsafe trim НЕ должен попадать в PWM.
  // До фикса UpdatePwm после SetPwmNeutral записывал SetPwm(0+trim, 0+trim).
  SetDirectLaw();
  auto cfg = stab_mgr_->GetConfig();
  cfg.throttle_trim = 0.1f;
  cfg.steering_trim = 0.05f;
  stab_mgr_->SetConfig(cfg);

  // Нет RC, нет Wi-Fi → failsafe на первом же шаге
  RunSteps(10);
  EXPECT_FLOAT_EQ(platform_.GetLastThrottle(), 0.0f)
      << "Моторы должны стоять в нейтрали, а не ползти на trim";
  EXPECT_FLOAT_EQ(platform_.GetLastSteering(), 0.0f);
}

TEST_F(ProcessorTest, FailsafeRecovery_TrimAppliedAgain) {
  // После восстановления сигнала trim снова применяется к PWM
  SetDirectLaw();
  auto cfg = stab_mgr_->GetConfig();
  cfg.throttle_trim = 0.1f;
  stab_mgr_->SetConfig(cfg);

  RunSteps(5);  // failsafe активен
  ASSERT_FLOAT_EQ(platform_.GetLastThrottle(), 0.0f);

  platform_.SetWifiCommand({0.5f, 0.0f});
  RunSteps(3);  // восстановление: Active → Recovering → Inactive
  EXPECT_NEAR(platform_.GetLastThrottle(), 0.5f + 0.1f, 1e-4f);
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

TEST_F(ProcessorTest, FailsafeClearsMotorModelSnapshot) {
  // LOS-246: после failsafe EKF не должен продолжать получать прошлый
  // throttle-якорь, пока PWM удерживается в нейтрали.
  SetDirectLaw();
  ImuHandler imu_handler(platform_, imu_calib_, madgwick_, 2);
  imu_handler.SetEnabled(true);
  ctx_->imu_handler = &imu_handler;

  auto cfg = stab_mgr_->GetConfig();
  cfg.filter.motor_deadzone = 0.0f;
  cfg.filter.motor_speed_gain = 8.0f;
  stab_mgr_->SetConfig(cfg);

  ImuData level{};
  level.az = 1.0f;
  platform_.SetImuData(level);
  platform_.SetWifiCommand({1.0f, 0.0f});
  Step();

  platform_.ClearWifiCommand();
  time_ms_ += 600;
  processor_->Step(time_ms_, 600);  // активирует failsafe и очищает snapshot
  Step();

  EXPECT_NEAR(ekf_.GetLastSpeedMeas(), 0.0f, 1e-6f);
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

TEST_F(ProcessorTest, MotorModelUsesCommandBeforeKidsSpeedLimiter) {
  // LOS-246: speed limiter меняет commanded_throttle_ в
  // UpdateStabilization(). Мотор-модель на следующем тике не должна читать
  // этот выход, иначе лимитер влияет на EKF-скорость, по которой сам же
  // регулирует.
  ImuHandler imu_handler(platform_, imu_calib_, madgwick_, 2);
  imu_handler.SetEnabled(true);
  ctx_->imu_handler = &imu_handler;
  kids_processor_.Init(ekf_, &imu_handler);

  auto cfg = stab_mgr_->GetConfig();
  cfg.mode = DriveMode::Kids;
  stab_mgr_->SetConfig(cfg);

  cfg = stab_mgr_->GetConfig();
  cfg.kids_mode.throttle_limit = 0.3f;
  cfg.kids_mode.slew_throttle = 2.0f;
  cfg.kids_mode.speed_limit_enabled = false;
  cfg.kids_mode.max_speed_ms = 0.5f;
  cfg.kids_mode.speed_limit_gain = 1.0f;
  cfg.kids_mode.anti_spin_enabled = false;
  cfg.kids_mode.accel_limit_enabled = false;
  cfg.slew_throttle = 100.0f;  // Kids-предел 2.0/с остаётся единственным slew
  cfg.filter.motor_deadzone = 0.0f;
  cfg.filter.motor_speed_gain = 8.0f;
  stab_mgr_->SetConfig(cfg);

  ImuData level{};
  level.az = 1.0f;
  platform_.SetImuData(level);
  platform_.SetWifiCommand({1.0f, 0.0f});

  RunSteps(100);  // единый PWM slew (Kids-предел 2.0/с) доходит до 0.3
  cfg.kids_mode.speed_limit_enabled = true;
  stab_mgr_->SetConfig(cfg);
  ekf_.SetState(2.0f, 0.0f, 0.0f);
  RunSteps(10);  // дождаться следующего внешнего PWM update
  ASSERT_TRUE(kids_processor_.IsSpeedLimitActive());
  ASSERT_LT(platform_.GetLastThrottle(), 0.3f);

  Step();
  EXPECT_NEAR(ekf_.GetLastSpeedMeas(), 2.4f, 1e-4f)
      << "мотор-модель не должна получать throttle после speed limiter";
}

TEST_F(ProcessorTest, KidsMotorModelTracksCounterfactualPwmSlew) {
  // LOS-246: внешний PWM slew применяется после speed limiter. Моторная
  // модель должна повторять этот slew для pre-limiter цели, иначе якорь
  // забегает вперёд реального мотора.
  ImuHandler imu_handler(platform_, imu_calib_, madgwick_, 2);
  imu_handler.SetEnabled(true);
  ctx_->imu_handler = &imu_handler;
  kids_processor_.Init(ekf_, &imu_handler);

  auto cfg = stab_mgr_->GetConfig();
  cfg.mode = DriveMode::Kids;
  stab_mgr_->SetConfig(cfg);
  cfg = stab_mgr_->GetConfig();
  cfg.kids_mode.throttle_limit = 0.3f;
  cfg.kids_mode.slew_throttle = 2.0f;
  cfg.kids_mode.speed_limit_enabled = true;
  cfg.kids_mode.max_speed_ms = 0.5f;
  cfg.kids_mode.speed_limit_gain = 1.0f;
  cfg.kids_mode.anti_spin_enabled = false;
  cfg.kids_mode.accel_limit_enabled = false;
  cfg.slew_throttle = 0.3f;
  cfg.filter.motor_deadzone = 0.0f;
  cfg.filter.motor_speed_gain = 8.0f;
  stab_mgr_->SetConfig(cfg);

  ImuData level{};
  level.az = 1.0f;
  platform_.SetImuData(level);
  platform_.SetWifiCommand({1.0f, 0.0f});

  RunSteps(9);
  ekf_.SetState(2.0f, 0.0f, 0.0f);  // limiter оставляет 15% цели
  Step();  // первый внешний PWM update: 0.3/s * 20ms = 0.006
  ASSERT_TRUE(kids_processor_.IsSpeedLimitActive());
  Step();

  EXPECT_NEAR(ekf_.GetLastSpeedMeas(), 0.006f * 8.0f, 1e-4f)
      << "Kids-якорь должен учитывать внешний PWM slew";
}

TEST_F(ProcessorTest, MotorModelKeepsOriginatingModeAcrossModeSwitch) {
  // Снимок читается в начале следующего тика. Если на этом тике сменить
  // Normal на Kids, он всё ещё обязан описывать PWM прошлого Normal-тика.
  ImuHandler imu_handler(platform_, imu_calib_, madgwick_, 2);
  imu_handler.SetEnabled(true);
  ctx_->imu_handler = &imu_handler;
  kids_processor_.Init(ekf_, &imu_handler);

  auto cfg = stab_mgr_->GetConfig();
  cfg.mode = DriveMode::Normal;
  cfg.slew_throttle = 0.1f;
  cfg.filter.motor_deadzone = 0.0f;
  cfg.filter.motor_speed_gain = 8.0f;
  stab_mgr_->SetConfig(cfg);

  ImuData level{};
  level.az = 1.0f;
  platform_.SetImuData(level);
  platform_.SetWifiCommand({1.0f, 0.0f});
  Step();  // внешний PWM ещё не обновлялся, итоговый снимок = 0

  cfg.mode = DriveMode::Kids;
  cfg.kids_mode.speed_limit_enabled = false;
  stab_mgr_->SetConfig(cfg);
  Step();

  EXPECT_NEAR(ekf_.GetLastSpeedMeas(), 0.0f, 1e-6f)
      << "при смене режима нельзя подставлять raw-команду прошлого тика";
}

TEST_F(ProcessorTest, MotorModelUsesAppliedThrottleOutsideKidsMode) {
  // В Normal raw-команда не должна обходить внешний PWM slew: моторная модель
  // видит applied_throttle_ прошлого тика, а не мгновенный full-stick.
  ImuHandler imu_handler(platform_, imu_calib_, madgwick_, 2);
  imu_handler.SetEnabled(true);
  ctx_->imu_handler = &imu_handler;

  auto cfg = stab_mgr_->GetConfig();
  cfg.mode = DriveMode::Normal;
  cfg.slew_throttle = 0.1f;
  cfg.filter.motor_deadzone = 0.0f;
  cfg.filter.motor_speed_gain = 8.0f;
  stab_mgr_->SetConfig(cfg);

  ImuData level{};
  level.az = 1.0f;
  platform_.SetImuData(level);
  platform_.SetWifiCommand({1.0f, 0.0f});

  Step();  // PWM update ещё не наступил: applied_throttle_ остаётся нулём
  Step();  // якорь должен прочитать именно этот нулевой applied output

  EXPECT_NEAR(ekf_.GetLastSpeedMeas(), 0.0f, 1e-6f)
      << "моторная модель в Normal обошла внешний PWM slew raw-командой";
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

TEST_F(ProcessorTest, KidsModeCapsGlobalSteeringSlewRate) {
  auto cfg = stab_mgr_->GetConfig();
  cfg.mode = DriveMode::Kids;
  ASSERT_TRUE(stab_mgr_->SetConfig(cfg));

  platform_.SetWifiCommand({0.0f, 1.0f});
  RunSteps(10);  // Первый PWM update через 20 ms.

  // Единственный PWM slew выбирает min(global=3.0, kids=3.0) = 3.0 /с.
  EXPECT_NEAR(platform_.GetLastSteering(), 0.06f, 0.005f);
}

// LOS-286: снятый мастер-выключатель снимает и Kids-потолок slew, иначе руль
// остался бы зажат при формально выключенных ограничителях.
TEST_F(ProcessorTest, KidsSlewCapSkippedWhenLimitersDisabled) {
  auto cfg = stab_mgr_->GetConfig();
  cfg.mode = DriveMode::Kids;
  ASSERT_TRUE(stab_mgr_->SetConfig(cfg));

  cfg = stab_mgr_->GetConfig();
  cfg.slew_steering = 3.0f;
  cfg.kids_mode.slew_steering = 0.5f;
  cfg.kids_mode.limiters_enabled = false;
  ASSERT_TRUE(stab_mgr_->SetConfig(cfg));

  platform_.SetWifiCommand({0.0f, 1.0f});
  RunSteps(10);  // Первый PWM update через 20 ms.

  // Kids-потолок 0.5 /с проигнорирован: работает global=3.0 → 0.06 за 20 ms.
  EXPECT_NEAR(platform_.GetLastSteering(), 0.06f, 0.005f);
}

TEST_F(ProcessorTest, KidsSlewCapAppliedWhenLimitersEnabled) {
  auto cfg = stab_mgr_->GetConfig();
  cfg.mode = DriveMode::Kids;
  ASSERT_TRUE(stab_mgr_->SetConfig(cfg));

  cfg = stab_mgr_->GetConfig();
  cfg.slew_steering = 3.0f;
  cfg.kids_mode.slew_steering = 0.5f;
  cfg.kids_mode.limiters_enabled = true;
  ASSERT_TRUE(stab_mgr_->SetConfig(cfg));

  platform_.SetWifiCommand({0.0f, 1.0f});
  RunSteps(10);

  // min(global=3.0, kids=0.5) * 20 ms = 0.01.
  EXPECT_NEAR(platform_.GetLastSteering(), 0.01f, 0.005f);
}

TEST_F(ProcessorTest, KidsModeUsesLowerGlobalSteeringSlewRate) {
  auto cfg = stab_mgr_->GetConfig();
  cfg.mode = DriveMode::Kids;
  ASSERT_TRUE(stab_mgr_->SetConfig(cfg));

  cfg = stab_mgr_->GetConfig();
  cfg.slew_steering = 1.0f;
  cfg.kids_mode.slew_steering = 3.0f;
  ASSERT_TRUE(stab_mgr_->SetConfig(cfg));

  platform_.SetWifiCommand({0.0f, 1.0f});
  RunSteps(10);

  // min(global=1.0, kids=3.0) * 20 ms = 0.02.
  EXPECT_NEAR(platform_.GetLastSteering(), 0.02f, 0.005f);
}

TEST_F(ProcessorTest, KidsModeBrakeUsesEffectiveThrottleSlewRate) {
  auto cfg = stab_mgr_->GetConfig();
  cfg.mode = DriveMode::Kids;
  ASSERT_TRUE(stab_mgr_->SetConfig(cfg));

  cfg = stab_mgr_->GetConfig();
  cfg.kids_mode.throttle_limit = 0.3f;
  cfg.kids_mode.slew_throttle = 0.3f;
  cfg.slew_throttle = 1.0f;
  cfg.kids_mode.anti_spin_enabled = false;
  cfg.kids_mode.accel_limit_enabled = false;
  cfg.braking_mode = BrakingMode::Brake;
  cfg.brake_slew_multiplier = 4.0f;
  ASSERT_TRUE(stab_mgr_->SetConfig(cfg));

  platform_.SetWifiCommand({0.3f, 0.0f});
  RunSteps(1000);
  EXPECT_NEAR(platform_.GetLastThrottle(), 0.3f, 0.02f);

  platform_.SetWifiCommand({0.0f, 0.0f});
  // min(global=1.0, kids=0.3) * 4 = 1.2 /с; 0.3 reaches zero in 250 ms.
  RunSteps(130);
  EXPECT_NEAR(platform_.GetLastThrottle(), 0.0f, 0.05f);
}

// ═══════════════════════════════════════════════════════════════════════════
// BrakingMode
// ═══════════════════════════════════════════════════════════════════════════

/** Включить BrakingMode::Brake в Normal mode (slew_throttle=0.5, mult=4 → 2/s)
 */
// Note: helper defined as free function to avoid name clash with member
static void SetBrakeMode(StabilizationManager& stab_mgr,
                         float multiplier = 4.0f) {
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

  TelemetryLogFrame frame{};
  ASSERT_TRUE(telem_mgr_->GetLogFrame(count - 1, frame));
  EXPECT_EQ(frame.zupt_status, static_cast<uint8_t>(ZuptStatus::Applied));
}

TEST_F(ProcessorTest, WithoutImu_TelemLogEmpty) {
  // Без IMU-хендлера imu_enabled=false → лог не пишется
  platform_.SetWifiCommand({0.0f, 0.0f});
  RunSteps(100);

  size_t count = 0, cap = 0;
  telem_mgr_->GetLogInfo(count, cap);
  EXPECT_EQ(count, 0u);
}

TEST_F(ProcessorTest, MadgwickDisabled_NoStaleGravityCompensation) {
  // LOS-232 (Codex P2): при выключенном в рантайме Madgwick кватернион залипает
  // на последнем (наклонном) значении. Grav-компенсация EKF не должна вычитать
  // эту устаревшую проекцию — иначе на ровной едущей машине появляется
  // фантомное ускорение и vx расходится. Проверяем, что чтение углов загейчено
  // на madgwick_enabled: наклонённый «протухший» AHRS не приводит к разгону.
  ImuHandler imu_handler(platform_, imu_calib_, madgwick_, 2);
  imu_handler.SetEnabled(true);
  ctx_->imu_handler = &imu_handler;

  // Накреняем Madgwick напрямую (нос вверх ~30°): ax=-sin, az=cos.
  constexpr float kPitch = 30.0f * 3.14159265358979f / 180.0f;
  for (int i = 0; i < 6000; ++i) {
    madgwick_.Update(-std::sin(kPitch), 0.0f, std::cos(kPitch), 0.0f, 0.0f,
                     0.0f, 0.002f);
  }
  float p = 0.0f, r = 0.0f, y = 0.0f;
  madgwick_.GetEulerRad(p, r, y);
  ASSERT_GT(std::abs(p), 20.0f * 3.14159265358979f / 180.0f)
      << "Пресет наклона не сошёлся — тест не проверяет то, что должен";

  // Выключаем Madgwick: и в конфиге (гейт grav-comp), и в хендлере (чтобы он не
  // перезаписал «протухшую» ориентацию ровными семплами на последующих шагах).
  // Мотор-модельный якорь отключаем, чтобы vx определялся ТОЛЬКО интеграцией
  // IMU — иначе якорь маскирует фантом и тест становится вакуумным.
  imu_handler.SetMadgwickEnabled(false);
  SetDirectLaw();  // без slew: throttle сразу 0.5 → ZUPT выключен
  auto cfg = stab_mgr_->GetConfig();
  cfg.filter.madgwick_enabled = false;
  cfg.filter.motor_model_enabled = false;
  stab_mgr_->SetConfig(cfg);

  // Ровная едущая машина: throttle 0.5 (ZUPT выключен), ускорение чисто
  // гравитационное по Z (ax=ay=0).
  platform_.SetWifiCommand({0.5f, 0.0f});
  ImuData imu{};
  imu.az = 1.0f;
  platform_.SetImuData(imu);

  RunSteps(2000);  // 4 c

  // С багом устаревший pitch=30° даёт фантом g·sin30≈4.9 м/с²: без якоря vx
  // интегрируется до клемпа kMaxSpeedMs и поднимает diverged. С фиксом углы
  // читаются как 0 → фантома нет → vx остаётся ≈0.
  EXPECT_FALSE(ekf_.IsDiverged());
  EXPECT_LT(ekf_.GetVx(), 1.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// TiltEstimator grav-comp (LOS-240)
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(ProcessorTest, TiltComp_LevelAccel_VxTracksTrueSpeed) {
  // Критерий приёмки LOS-240: прямой разгон 0.2g/5с на РОВНОМ не должен
  // «съедаться» grav-comp. С Madgwick как источником тангажа (баг из
  // код-ревью #287) |a|≈1.02g остаётся ниже порога adaptive-beta и Madgwick
  // заваливает pitch на ~11°, из-за чего EKF vx сильно недооценивает
  // истинную скорость. TiltEstimator держит pitch≈0 на гироскопе.
  ImuHandler imu_handler(platform_, imu_calib_, madgwick_, 2);
  imu_handler.SetEnabled(true);
  ctx_->imu_handler = &imu_handler;

  SetDirectLaw();  // throttle сразу применяется, без slew
  auto cfg = stab_mgr_->GetConfig();
  cfg.filter.tilt_comp_enabled = true;
  // Мотор-модельный якорь отключаем: иначе он подтянет vx к ожидаемой
  // скорости независимо от корректности grav-comp, и тест станет вакуумным
  // (как и в MadgwickDisabled_NoStaleGravityCompensation выше). Без якоря
  // a_lin_g принудительно 0 (код-ревью PR #290, 4-й раунд: подача EKF vx
  // обратно в TiltEstimator без независимого якоря — самоподтверждающаяся
  // циркулярность, а не защита), поэтому TiltEstimator честно деградирует
  // до гиро + негейтированной accel-коррекции — медленнее, чем с якорем
  // (см. TiltComp_YawedMount ниже), но всё ещё на порядок лучше исходного
  // бага с Madgwick (было ~1.1 м/с).
  cfg.filter.motor_model_enabled = false;
  stab_mgr_->SetConfig(cfg);

  // throttle > 2% отключает ZUPT; машина «едет прямо» с постоянным
  // продольным ускорением 0.2g на ровном месте (ay=0, gx=gy=gz=0).
  platform_.SetWifiCommand({0.5f, 0.0f});
  ImuData imu{};
  imu.ax = 0.2f;
  imu.az = 1.0f;
  platform_.SetImuData(imu);

  RunSteps(2500);  // 5 секунд при dt=2мс

  EXPECT_FALSE(ekf_.IsDiverged());
  EXPECT_GT(ekf_.GetVx(), 2.5f)
      << "vx не должен быть «съеден» ложной grav-компенсацией (было ~1.1 "
         "м/с при баге с Madgwick — см. код-ревью PR #287)";
}

TEST_F(ProcessorTest, TiltComp_StaticTilt_NoDivergence) {
  // Дополняет тест выше: одновременно с разгоном на ровном grav-comp должна
  // продолжать защищать от статического наклона (критерий приёмки LOS-240
  // требует прохождения обоих сценариев).
  //
  // Код-ревью PR #290 (4-й раунд): без независимого якоря (motor_model
  // выключен здесь ровно как выше) EKF vx НЕ является независимым
  // источником a_lin для TiltEstimator (сам зависит от текущего тангажа
  // через grav_x/grav_y в этом же UpdateFromImu) — feedback через него
  // самоподтверждается на любом уровне остаточной ошибки. Раньше a_lin в
  // этом случае всё равно брался из EKF vx — на статике 20° без якоря при
  // throttle>2% (ZUPT выключен) это уводило vx в клемп kMaxSpeedMs=-15
  // (проверено эмпирически). Фикс: без якоря a_lin принудительно 0 —
  // TiltEstimator деградирует до гиро + негейтированной accel-коррекции,
  // vx получает СТАБИЛЬНОЕ (не нулевое — нет якоря, тянущего к 0) смещение
  // за время сходимости тангажа к истине, но БЕЗ разгона к клемпу.
  // Раньше проверялся только EXPECT_LT(vx, 5.0) — не ловит уход в БОЛЬШОЙ
  // ОТРИЦАТЕЛЬНЫЙ vx (ровно то, что делал баг здесь). Проверяем |vx|.
  ImuHandler imu_handler(platform_, imu_calib_, madgwick_, 2);
  imu_handler.SetEnabled(true);
  ctx_->imu_handler = &imu_handler;

  SetDirectLaw();
  auto cfg = stab_mgr_->GetConfig();
  cfg.filter.tilt_comp_enabled = true;
  cfg.filter.motor_model_enabled = false;
  stab_mgr_->SetConfig(cfg);

  // throttle > 2% отключает ZUPT — иначе тест грав-компенсации был бы
  // вакуумным (ZUPT сам обнулил бы vx независимо от корректности тангажа).
  platform_.SetWifiCommand({0.5f, 0.0f});
  constexpr float kPitch = 20.0f * 3.14159265358979f / 180.0f;
  ImuData imu{};
  imu.ax = -std::sin(kPitch);
  imu.az = std::cos(kPitch);
  platform_.SetImuData(imu);

  RunSteps(4000);  // 8 секунд — достаточно для полной сходимости тангажа
                   // (corr_gain_hz=0.5 по умолчанию) и выхода vx на плато.

  EXPECT_FALSE(ekf_.IsDiverged());
  // Без grav-comp фантомное ускорение g·sin(20°)≈3.35 м/с² ушло бы к клемпу
  // kMaxSpeedMs=15 за секунды. С фиксом vx выходит на стабильное плато
  // заметно меньшей амплитуды (не расходится) — не идеально (нет якоря,
  // возвращающего vx к 0), но принципиально иное поведение, чем клемп/разнос.
  EXPECT_LT(std::abs(ekf_.GetVx()), 10.0f);
}

TEST_F(ProcessorTest, TiltComp_YawedMount_VxTracksTrueSpeedNotVy) {
  // Код-ревью PR #290 (3-й раунд): EKF получал сенсорные ax/ay напрямую,
  // тогда как grav_x/grav_y (из pitch_rad/roll_rad) уже в СК машины —
  // рассинхронизация СК. При IMU, повёрнутом на 90° по yaw (Forward-
  // калибровка существует именно для произвольного разворота IMU на плате —
  // не гипотетический случай), истинное продольное ускорение приходит в
  // сенсорную ay. Без поворота ax/ay перед UpdateFromImu оно ушло бы в vy
  // вместо vx.
  ImuCalibData calib_data{};
  calib_data.valid = true;
  calib_data.gravity_vec[2] = 1.f;
  calib_data.accel_forward_vec[1] = 1.f;  // «вперёд» машины = сенсорная Y
  imu_calib_.SetData(calib_data);

  ImuHandler imu_handler(platform_, imu_calib_, madgwick_, 2);
  imu_handler.SetEnabled(true);
  ctx_->imu_handler = &imu_handler;

  SetDirectLaw();
  auto cfg = stab_mgr_->GetConfig();
  cfg.filter.tilt_comp_enabled = true;
  // Как и в TiltComp_LevelAccel выше: без якоря a_lin_g принудительно 0
  // (код-ревью PR #290, 4-й раунд) — критерий ослаблен относительно
  // «якорь есть» случая, но остаётся на порядок лучше бага (vx≈0 без фикса
  // ротации ax/ay, т.к. ускорение целиком уходило в vy).
  cfg.filter.motor_model_enabled = false;
  stab_mgr_->SetConfig(cfg);

  platform_.SetWifiCommand({0.5f, 0.0f});
  ImuData imu{};
  imu.ay = 0.2f;  // истинное продольное 0.2g читается сенсором как ay
  imu.az = 1.0f;
  platform_.SetImuData(imu);

  RunSteps(2500);  // 5 секунд

  EXPECT_FALSE(ekf_.IsDiverged());
  EXPECT_GT(ekf_.GetVx(), 2.0f)
      << "продольное ускорение не должно уходить в vy на yaw-развёрнутом "
         "монтаже";
}

TEST_F(ProcessorTest, TiltComp_ReEnabled_ResetsStaleState) {
  // Код-ревью PR #290 (9-й раунд): tilt_comp_enabled переключается в
  // рантайме конфигом. Пока выключен, tilt_est_.Update() не вызывается —
  // её pitch_rad_/roll_rad_ замораживаются на последнем значении. Без
  // сброса при повторном включении EKF на первом же тике получил бы
  // протухший тангаж из интервала, пока фильтр был выключен.
  ImuHandler imu_handler(platform_, imu_calib_, madgwick_, 2);
  imu_handler.SetEnabled(true);
  ctx_->imu_handler = &imu_handler;

  SetDirectLaw();
  auto cfg = stab_mgr_->GetConfig();
  cfg.filter.tilt_comp_enabled = true;
  cfg.filter.motor_model_enabled = false;
  stab_mgr_->SetConfig(cfg);

  // Фаза 1: статический наклон 20° — даём tilt_est_ сойтись близко к 20°.
  platform_.SetWifiCommand({0.5f, 0.0f});  // throttle>2% отключает ZUPT
  constexpr float kPitch = 20.0f * 3.14159265358979f / 180.0f;
  ImuData tilted{};
  tilted.ax = -std::sin(kPitch);
  tilted.az = std::cos(kPitch);
  platform_.SetImuData(tilted);
  RunSteps(4000);  // 8 секунд

  // Фаза 2: выключаем tilt-фильтр (и Madgwick-фолбэк) и кладём машину
  // ровно — Update() больше не вызывается, pitch_rad_ должен остаться
  // замороженным на ~20°, несмотря на то что реальный наклон теперь 0.
  cfg = stab_mgr_->GetConfig();
  cfg.filter.tilt_comp_enabled = false;
  cfg.filter.madgwick_enabled = false;
  stab_mgr_->SetConfig(cfg);
  ImuData level{};
  level.az = 1.0f;
  platform_.SetImuData(level);
  RunSteps(100);

  // Фаза 3: заново включаем tilt-фильтр на РОВНОМ месте без реального
  // ускорения. Сбрасываем EKF, чтобы изолировать именно эффект
  // протухшего тангажа (иначе фаза 1 уже увела vx далеко от нуля).
  ekf_.Reset();
  cfg = stab_mgr_->GetConfig();
  cfg.filter.tilt_comp_enabled = true;
  stab_mgr_->SetConfig(cfg);
  RunSteps(50);  // 0.1 секунды — достаточно, чтобы проявился фантом

  EXPECT_FALSE(ekf_.IsDiverged());
  // Без фикса протухший pitch≈20° даёт фантомное ускорение ~3.35 м/с²,
  // т.е. за 0.1с — заметный уход vx. С фиксом (Reset() при повторном
  // включении) pitch стартует с 0 — vx остаётся близко к нулю.
  EXPECT_NEAR(ekf_.GetVx(), 0.0f, 0.15f)
      << "протухший тангаж после повторного включения tilt-фильтра";
}

TEST_F(ProcessorTest, TiltComp_EkfDisabled_KeepsTrackingNotFrozen) {
  // Код-ревью PR #302 (круг 2, найдено независимо ревьюером и ботом Codex):
  // до этой правки ekf_enabled=false замораживал tilt_est_.Update() тем же
  // путём, что и tilt_comp_enabled=false (см. TiltComp_ReEnabled_
  // ResetsStaleState выше) — весь блок UpdateSensorsAndEkf() был вложен под
  // `if (ekf_active && ...)`. Это ломало НЕ ТОЛЬКО grav-компенсацию EKF, но
  // и fwd_accel_g_ (LOS-245): при выключенном EKF Kids-лимитер снова ложно
  // срабатывал на статическом наклоне — ровно тот баг, который чинит
  // LOS-245 (см. KidsAccelLimitTest.EkfDisabled_TiltCompEnabled_
  // StillCompensatesTilt ниже, где это проверяется напрямую).
  //
  // Тест ниже проверяет, что ПОСЛЕ фикса tilt_est_ ПРОДОЛЖАЕТ отслеживать
  // реальную ориентацию, пока EKF выключен, а не замораживается: наклон
  // меняется с 20° на 0° именно В ЭТОМ интервале, и к моменту повторного
  // включения EKF оценка должна успеть сойтись к истинному (нулевому)
  // значению без явного Reset() — в отличие от TiltComp_ReEnabled_
  // ResetsStaleState, где Reset() по-прежнему необходим (там триггер —
  // tilt_comp_enabled=false, который ВСЁ ЕЩЁ замораживает tilt_est_).
  ImuHandler imu_handler(platform_, imu_calib_, madgwick_, 2);
  imu_handler.SetEnabled(true);
  ctx_->imu_handler = &imu_handler;

  SetDirectLaw();
  auto cfg = stab_mgr_->GetConfig();
  cfg.filter.tilt_comp_enabled = true;
  cfg.filter.motor_model_enabled = false;
  stab_mgr_->SetConfig(cfg);

  // Фаза 1: статический наклон 20° — даём tilt_est_ сойтись близко к 20°.
  platform_.SetWifiCommand({0.5f, 0.0f});
  constexpr float kPitch = 20.0f * 3.14159265358979f / 180.0f;
  ImuData tilted{};
  tilted.ax = -std::sin(kPitch);
  tilted.az = std::cos(kPitch);
  platform_.SetImuData(tilted);
  RunSteps(4000);  // 8 секунд

  // Фаза 2: выключаем EKF целиком (tilt_comp_enabled остаётся true!) и
  // кладём машину ровно — ДОСТАТОЧНО ДОЛГО (corr_gain_hz=0.5 по умолчанию
  // ⇒ постоянная времени 2 с), чтобы tilt_est_ реально сошёлся к 0°, если
  // он продолжает работать. Раньше эта фаза была короткой (0.2 с) — этого
  // хватало только чтобы проверить, что Reset() в фазе 3 корректно стирает
  // заморозку; теперь тут ничего замораживать не нужно.
  cfg = stab_mgr_->GetConfig();
  cfg.filter.ekf_enabled = false;
  stab_mgr_->SetConfig(cfg);
  ImuData level{};
  level.az = 1.0f;
  platform_.SetImuData(level);
  RunSteps(3000);  // 6 секунд — ~3 постоянные времени

  // Фаза 3: заново включаем EKF на ровном месте без реального ускорения.
  ekf_.Reset();
  cfg = stab_mgr_->GetConfig();
  cfg.filter.ekf_enabled = true;
  stab_mgr_->SetConfig(cfg);
  RunSteps(50);  // 0.1 секунды

  EXPECT_FALSE(ekf_.IsDiverged());
  EXPECT_NEAR(ekf_.GetVx(), 0.0f, 0.15f)
      << "tilt_est_ не сошёлся к истинному (нулевому) наклону за время, "
         "пока EKF был выключен — оценка осталась протухшей";
}

// ═══════════════════════════════════════════════════════════════════════════
// CalibrationManager
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(ProcessorTest, CalibMgr_Null_NoCrash) {
  // Пересобрать без calib_mgr
  ControlLoopContext ctx{
      platform_,        imu_calib_,   madgwick_,  ekf_,
      yaw_ctrl_,        pitch_ctrl_,  slip_ctrl_, oversteer_guard_,
      kids_processor_,  auto_drive_,  nullptr,    stab_mgr_.get(),
      telem_mgr_.get(), nullptr,      nullptr,    nullptr,
      nullptr,          last_loop_hz_};
  ControlLoopProcessor proc(ctx, 0);
  EXPECT_NO_THROW(proc.Step(2, 2));
}

// ═══════════════════════════════════════════════════════════════════════════
// Kids Mode: accel-лимитер и тангаж (LOS-245)
//
// GetForwardAccel() снимал гравитацию по КОНСТАНТНОМУ RestDownVec(), то есть
// считал машину всегда горизонтальной, и при тангаже отдавал sin(pitch)·g
// вместо ускорения. По логу ночного заезда 25.07 утечка превышала порог
// accel-лимитера (0.15 g ⇔ тангаж 8.6°) в 8.9 % сэмплов, тогда как реальное
// продольное ускорение имело медиану 0.010 g. Отдельная фикстура: ProcessorTest
// собран без ImuHandler (imu_enabled=false), а здесь нужен живой IMU-путь.
// ═══════════════════════════════════════════════════════════════════════════

class KidsAccelLimitTest : public ::testing::Test {
 protected:
  void SetUp() override {
    stab_mgr_ = std::make_unique<StabilizationManager>(
        platform_, madgwick_, yaw_ctrl_, slip_ctrl_, nullptr);
    calib_mgr_ = std::make_unique<CalibrationManager>(platform_, imu_calib_,
                                                      madgwick_, &ekf_);
    wifi_handler_ = std::make_unique<WifiCommandHandler>(platform_, 500);
    imu_handler_ =
        std::make_unique<ImuHandler>(platform_, imu_calib_, madgwick_, 2);
    imu_handler_->SetEnabled(true);
    telem_mgr_ = std::make_unique<TelemetryManager>();
    telem_mgr_->Init(1000);
    // send_interval_ms=0: каждый тик публикует снимок, чтобы тесты могли
    // читать forward_accel через platform_.GetLastSnap() без гонки с
    // троттлингом отправки.
    telem_handler_ = std::make_unique<TelemetryHandler>(platform_, 0);
    auto_drive_.SetCalibrationManager(calib_mgr_.get());

    ctx_ = std::make_unique<ControlLoopContext>(ControlLoopContext{
        platform_, imu_calib_, madgwick_, ekf_, yaw_ctrl_, pitch_ctrl_,
        slip_ctrl_, oversteer_guard_, kids_processor_, auto_drive_,
        calib_mgr_.get(), stab_mgr_.get(), telem_mgr_.get(), nullptr,
        wifi_handler_.get(), imu_handler_.get(), telem_handler_.get(),
        last_loop_hz_});
    processor_ = std::make_unique<ControlLoopProcessor>(*ctx_, 0);

    auto cfg = stab_mgr_->GetConfig();
    cfg.mode = DriveMode::Kids;
    stab_mgr_->SetConfig(cfg);
  }

  void RunSteps(uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) {
      time_ms_ += 2;
      processor_->Step(time_ms_, 2);
    }
  }

  /** Показания IMU в покое при заданном тангаже [рад].
   *  Нос ВНИЗ = pitch < 0 → ax > 0, то есть клевок читается как «разгон». */
  static ImuData AtRestWithPitch(float pitch_rad) {
    ImuData d{};
    d.ax = -std::sin(pitch_rad);
    d.az = std::cos(pitch_rad);
    return d;
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
  std::unique_ptr<ImuHandler> imu_handler_;
  std::unique_ptr<TelemetryManager> telem_mgr_;
  std::unique_ptr<TelemetryHandler> telem_handler_;
  std::unique_ptr<ControlLoopContext> ctx_;
  std::unique_ptr<ControlLoopProcessor> processor_;

  uint32_t time_ms_{0};
};

TEST_F(KidsAccelLimitTest, StaticNoseDownTilt_DoesNotTriggerAccelLimit) {
  constexpr float kPitchRad = -15.f * 3.14159265358979f / 180.f;
  const ImuData tilted = AtRestWithPitch(kPitchRad);

  // Дискриминативность: прежний путь на СТОЯЩЕЙ машине выдавал ускорение выше
  // порога лимитера — этот тест падает на реализации до LOS-245.
  EXPECT_GT(imu_calib_.GetForwardAccel(tilted), 0.15f);

  // Фаза 1: машина стоит носом вниз, газа нет — TiltEstimator сходится к
  // −15°. При corr_gain_hz=0.5 постоянная времени 2 с, 6 с дают ~95 %.
  platform_.SetImuData(tilted);
  platform_.SetWifiCommand(RcCommand{0.0f, 0.0f});
  RunSteps(3000);

  // Фаза 2: даём газ, оставаясь на том же уклоне. Ускорения по-прежнему нет —
  // акселерометр показывает только проекцию гравитации.
  platform_.SetWifiCommand(RcCommand{0.5f, 0.0f});
  RunSteps(200);

  EXPECT_FALSE(kids_processor_.IsAccelLimitActive())
      << "accel-лимитер сработал на статическом наклоне без ускорения";
}

TEST_F(KidsAccelLimitTest, RealAccelerationOnSlope_StillTriggersAccelLimit) {
  // Обратная сторона: компенсация не должна ослеплять лимитер там, где
  // ускорение настоящее.
  constexpr float kPitchRad = -15.f * 3.14159265358979f / 180.f;

  platform_.SetImuData(AtRestWithPitch(kPitchRad));
  platform_.SetWifiCommand(RcCommand{0.0f, 0.0f});
  RunSteps(3000);

  ImuData accelerating = AtRestWithPitch(kPitchRad);
  accelerating.ax += 0.4f;  // реальный разгон 0.4 g поверх уклона
  platform_.SetImuData(accelerating);
  platform_.SetWifiCommand(RcCommand{0.5f, 0.0f});
  RunSteps(50);  // коротко: TiltEstimator не успевает «съесть» разгон

  EXPECT_TRUE(kids_processor_.IsAccelLimitActive())
      << "лимитер пропустил реальный разгон 0.4 g";
}

TEST_F(KidsAccelLimitTest, TiltCompDisabled_DegradesToLegacyNotMadgwick) {
  // При выключенном tilt-фильтре компенсации быть НЕ должно: фолбэка на
  // Madgwick здесь нет намеренно (он заваливается на разгоне — LOS-240).
  // Проверяем именно это: на статическом наклоне лимитер срабатывает, как и
  // до LOS-245. Через Madgwick он бы, наоборот, промолчал — то есть тест
  // отличает «деградировали в старое поведение» от «тихо взяли Madgwick».
  auto cfg = stab_mgr_->GetConfig();
  cfg.filter.tilt_comp_enabled = false;
  cfg.filter.madgwick_enabled = true;  // Madgwick жив, но не должен влиять
  stab_mgr_->SetConfig(cfg);

  constexpr float kPitchRad = -15.f * 3.14159265358979f / 180.f;
  platform_.SetImuData(AtRestWithPitch(kPitchRad));
  platform_.SetWifiCommand(RcCommand{0.5f, 0.0f});
  RunSteps(3000);  // с запасом на сходимость Madgwick

  EXPECT_TRUE(kids_processor_.IsAccelLimitActive())
      << "без tilt-фильтра ожидалось прежнее поведение, а компенсация всё же "
         "произошла — вероятно, вернулся фолбэк на Madgwick";
}

TEST_F(KidsAccelLimitTest, SustainedAcceleration_LimiterFadesAsTiltAbsorbsIt) {
  // Фиксация ИЗВЕСТНОГО ОГРАНИЧЕНИЯ, а не желаемого поведения. TiltEstimator
  // не отличает устойчивое продольное ускорение от наклона и с постоянной
  // времени 1/corr_gain_hz (по умолчанию 2 с) уводит его в тангаж. Замер:
  // при 0.3 g лимитер держится ~2.4 с, затем слепнет. Kids-режим ловит
  // короткие тычки газом, так что запаса хватает, но если тест упадёт —
  // значит характеристика фильтра поехала, и это надо осознать, а не
  // подкрутить константы.
  ImuData accelerating{};
  accelerating.ax = 0.3f;  // вдвое выше порога 0.15 g
  accelerating.az = 1.0f;
  platform_.SetImuData(accelerating);
  platform_.SetWifiCommand(RcCommand{0.5f, 0.0f});

  RunSteps(250);  // 0.5 с — разгон ещё виден
  EXPECT_TRUE(kids_processor_.IsAccelLimitActive())
      << "лимитер не поймал разгон 0.3 g даже в первые 0.5 с";

  RunSteps(2250);  // суммарно 5 с — тангаж «съел» ускорение
  EXPECT_FALSE(kids_processor_.IsAccelLimitActive())
      << "ограничение изменилось: разгон всё ещё виден через 5 с";
}

TEST_F(KidsAccelLimitTest, EkfDisabled_TiltCompEnabled_StillCompensatesTilt) {
  // Регресс код-ревью PR #302 (круг 2, найдено независимо ревьюером и ботом
  // Codex): tilt_est_.Update() был вложен в `if (ekf_active && ...)`, поэтому
  // при ekf_enabled=false (реальная, переключаемая через WS/мобильное
  // приложение настройка — НЕ гипотетическая) tilt_est_ замораживался, и
  // fwd_accel_g_ ниже деградировал в ImuCalibration::GetForwardAccel() —
  // то есть В ТОЧНОСТИ в баг, который чинит LOS-245: лимитер снова ложно
  // срабатывал на статическом наклоне. Фикс — считать tilt_est_.Update()
  // независимо от ekf_active (см. control_loop_processor.cpp).
  auto cfg = stab_mgr_->GetConfig();
  cfg.filter.ekf_enabled = false;       // отключено пользователем/оператором
  cfg.filter.tilt_comp_enabled = true;  // но tilt-фильтр формально включён

  stab_mgr_->SetConfig(cfg);

  constexpr float kPitchRad = -15.f * 3.14159265358979f / 180.f;
  platform_.SetImuData(AtRestWithPitch(kPitchRad));
  platform_.SetWifiCommand(RcCommand{0.5f, 0.0f});
  RunSteps(3000);  // 6 с — сходимость TiltEstimator не зависит от EKF

  EXPECT_FALSE(kids_processor_.IsAccelLimitActive())
      << "при выключенном EKF лимитер сработал на статическом наклоне без "
         "ускорения — tilt_est_ снова заморожен";
}

TEST_F(KidsAccelLimitTest, EkfDisabledMidTurn_StaleVxDoesNotCorruptTilt) {
  // Регресс код-ревью PR #302 (круг 3, найдено ботом Codex): a_lin_lat_g в
  // tilt_est_.Update() читал prev_vx_ БЕЗ гейта на ekf_active — после
  // разъединения tilt_est_ от ekf_active (круг 2) это давало ПРОТУХШЕЕ на
  // неопределённый срок значение, если EKF выключили на ходу (prev_vx_ != 0).
  // ay_grav = imu.ay − a_lin_lat_g влияет не только на roll (как думалось
  // изначально), но и на accel_mag (гейт коррекции) и horiz — а значит и на
  // pitch_acc = atan2(−ax_grav, horiz) в tilt_estimator.cpp: протухший
  // prev_vx_ мог исказить ТАНГАЖ и, как следствие, fwd_accel_g_/Kids-лимитер.
  //
  // Прямая проверка через IsAccelLimitActive() на статическом наклоне без
  // поворота недискриминативна: при ax_grav=0 (нет реального тангажа) atan2(0,
  // horiz)=0 при ЛЮБОМ horiz, искажение a_lin_lat_g не проявляется. Поэтому
  // здесь читаем forward_accel из телеметрии напрямую (platform_.GetLastSnap()
  // ), а не бинарный флаг.
  //
  // Сценарий: разгоняемся до ненулевой vx (мотор-модельный якорь через EKF)
  // на ровной дороге, выключаем EKF на ходу (prev_vx_ замораживается на
  // достигнутом значении), затем ставим машину на РЕАЛЬНЫЙ статический
  // наклон (как в StaticNoseDownTilt_DoesNotTriggerAccelLimit) и одновременно
  // поворачиваем — ax_grav теперь ненулевой.
  //
  // На практике эффект оказался ГРУБЕЕ, чем изначально предполагалось (не
  // тонкое смещение pitch_acc через horiz): при реалистичных vx~3.8 м/с и
  // gz=200°/с протухший a_lin_lat_g ≈ (gz·vx)/g ≈ 1.35g — это НА ПОРЯДОК
  // больше accel_gate_band_g (0.1 по умолчанию), поэтому accel_mag гейт
  // отвергает КАЖДУЮ коррекцию, и tilt_est_ бесконечно замораживается на
  // значении с момента выключения EKF (здесь — уровень/0°), полностью
  // игнорируя реальный наклон -15°. Без фикса forward_accel остаётся равным
  // сырому ax (~0.259g, что уже само по себе выше порога лимитера 0.15g) и
  // НЕ убывает даже за 10 секунд.
  ImuData level{};
  level.az = 1.0f;
  platform_.SetImuData(level);
  platform_.SetWifiCommand(RcCommand{0.5f, 0.0f});
  RunSteps(3000);  // 6 с: мотор-модельный якорь разгоняет EKF vx

  ASSERT_GT(ekf_.GetVx(), 0.5f)
      << "не удалось разогнать EKF vx в подготовке теста — сценарий "
         "непроверяем";

  auto cfg = stab_mgr_->GetConfig();
  cfg.filter.ekf_enabled = false;  // prev_vx_ замораживается прямо на ходу
  stab_mgr_->SetConfig(cfg);

  constexpr float kPitchRad = -15.f * 3.14159265358979f / 180.f;
  ImuData tilted_turning = AtRestWithPitch(kPitchRad);
  tilted_turning.gz = 200.0f;  // ощутимый поворот на том же уклоне
  platform_.SetImuData(tilted_turning);
  RunSteps(5000);  // 10 с — дать TiltEstimator сойтись к (возможно смещённому)
                   // значению под постоянной контаминацией a_lin_lat_g
                   // (corr_gain_hz=0.5 ⇒ постоянная времени 2с, 10с даёт
                   // <1% остатка — 6с оставляли ~5%, шумевшие в допуске)

  const float forward_accel = platform_.GetLastSnap().forward_accel;
  EXPECT_NEAR(forward_accel, 0.0f, 0.01f)
      << "протухший prev_vx_ исказил тангаж через a_lin_lat_g: "
         "forward_accel="
      << forward_accel
      << "g на статическом наклоне без реального продольного ускорения "
         "(должно быть ~0)";
}

// ═══════════════════════════════════════════════════════════════════════════
// LOS-219: профайлер — outlier по реальному периоду между вызовами Step()
//
// Собирается только при -DRC_PROFILE_LOOP=1 (см. tests/CMakeLists.txt).
// FakePlatform::GetTimeUs() читает СВОИ внутренние часы (platform_.time_ms_),
// независимые от аргументов now/dt_ms, передаваемых в Step() напрямую —
// поэтому для этих тестов часы платформы двигаются явно через
// platform_.AdvanceTimeMs(), отдельно от фикстурного time_ms_/Step().
// ═══════════════════════════════════════════════════════════════════════════
#ifdef RC_PROFILE_LOOP

class ProfilerTest : public ProcessorTest {
 protected:
  /** Шаг с явным приращением часов платформы (мс) — не путать с Step(). */
  void StepPlatformGap(uint32_t platform_gap_ms) {
    platform_.AdvanceTimeMs(platform_gap_ms);
    time_ms_ += 2;
    processor_->Step(time_ms_, 2);
  }

  /** Найти последнюю строку "PROF(max us): ..." среди залогированного. */
  std::optional<std::string> FindLastProfMaxLine() const {
    std::optional<std::string> result;
    for (const auto& msg : platform_.GetLoggedMessages()) {
      if (msg.find("PROF(max us)") != std::string::npos) result = msg;
    }
    return result;
  }

  /** Гонять до гарантированного пересечения границы диаг-интервала. */
  void RunUntilDiagIntervalCrossed() {
    while (time_ms_ < config::DiagnosticsConfig::kIntervalMs + 10) {
      StepPlatformGap(2);
    }
  }
};

TEST_F(ProfilerTest, NoOutliers_OnSteadyPlatformClock) {
  RunUntilDiagIntervalCrossed();

  auto line = FindLastProfMaxLine();
  ASSERT_TRUE(line.has_value()) << "PROF(max us) line never logged";
  EXPECT_NE(line->find("outliers=0/"), std::string::npos) << *line;
}

TEST_F(ProfilerTest, CountsOutlier_OnRealClockGap) {
  // Один разовый скачок часов ПЛАТФОРМЫ, сильно выше 2x бюджета цикла
  // (4000 мкс) — должен быть учтён как outlier, даже несмотря на то что
  // аргументы Step() (now/dt_ms) идут обычным равномерным тактом.
  bool gap_injected = false;
  while (time_ms_ < config::DiagnosticsConfig::kIntervalMs + 10) {
    if (!gap_injected && time_ms_ >= 1000) {
      StepPlatformGap(20);  // 20мс >> 4мс порога
      gap_injected = true;
    } else {
      StepPlatformGap(2);
    }
  }
  ASSERT_TRUE(gap_injected);

  auto line = FindLastProfMaxLine();
  ASSERT_TRUE(line.has_value()) << "PROF(max us) line never логировалась";
  EXPECT_NE(line->find("outliers=1/"), std::string::npos) << *line;
}

TEST_F(ProfilerTest, FirstStep_DoesNotFalselyReportOutlier) {
  // prof_prev_entry_us_ инициализируется 0 (сентинел "ещё не установлен") —
  // первый вызов Step() не должен подхватить псевдо-огромный период
  // (GetTimeUs() - 0), даже если платформенные часы уже не в нуле.
  platform_.AdvanceTimeMs(10000);
  StepPlatformGap(2);

  RunUntilDiagIntervalCrossed();

  auto line = FindLastProfMaxLine();
  ASSERT_TRUE(line.has_value());
  EXPECT_NE(line->find("outliers=0/"), std::string::npos) << *line;
}

#endif  // RC_PROFILE_LOOP
