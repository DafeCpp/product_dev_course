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

TEST_F(ProcessorTest, TiltComp_EkfReEnabled_ResetsStaleState) {
  // Код-ревью PR #290 (10-й раунд): тот же протухший-тангаж баг, что и в
  // TiltComp_ReEnabled_ResetsStaleState выше, но триггер — не
  // tilt_comp_enabled, а ekf_enabled (tilt_comp_enabled остаётся true
  // ВСЁ ВРЕМЯ). Пока ekf_enabled=false, весь блок UpdateSensorsAndEkf()
  // пропускается — включая tilt_est_.Update() — тем же путём замораживая
  // pitch_rad_/roll_rad_.
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
  // кладём машину ровно.
  cfg = stab_mgr_->GetConfig();
  cfg.filter.ekf_enabled = false;
  stab_mgr_->SetConfig(cfg);
  ImuData level{};
  level.az = 1.0f;
  platform_.SetImuData(level);
  RunSteps(100);

  // Фаза 3: заново включаем EKF на ровном месте без реального ускорения.
  ekf_.Reset();
  cfg = stab_mgr_->GetConfig();
  cfg.filter.ekf_enabled = true;
  stab_mgr_->SetConfig(cfg);
  RunSteps(50);  // 0.1 секунды

  EXPECT_FALSE(ekf_.IsDiverged());
  EXPECT_NEAR(ekf_.GetVx(), 0.0f, 0.15f)
      << "протухший тангаж после повторного включения EKF (tilt_comp_"
         "enabled не менялся)";
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
    auto_drive_.SetCalibrationManager(calib_mgr_.get());

    ctx_ = std::make_unique<ControlLoopContext>(ControlLoopContext{
        platform_, imu_calib_, madgwick_, ekf_, yaw_ctrl_, pitch_ctrl_,
        slip_ctrl_, oversteer_guard_, kids_processor_, auto_drive_,
        calib_mgr_.get(), stab_mgr_.get(), telem_mgr_.get(), nullptr,
        wifi_handler_.get(), imu_handler_.get(), nullptr, last_loop_hz_});
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
