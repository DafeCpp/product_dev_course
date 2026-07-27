#include <gtest/gtest.h>

#include <stdexcept>

#include "mock_platform.hpp"
#include "vehicle_control_unified.hpp"

using namespace rc_vehicle;
using namespace rc_vehicle::testing;

// ══════════════════════════════════════════════════════════════════════════════
// SimPlatform — FakePlatform с ограниченным числом итераций control loop
// ══════════════════════════════════════════════════════════════════════════════

struct StopLoopException : std::exception {};

class SimPlatform : public FakePlatform {
 public:
  explicit SimPlatform(uint32_t max_iterations)
      : max_iterations_(max_iterations) {}

  std::expected<void, PlatformError> CreateTask(void (*entry)(void*),
                                                void* arg) override {
    // Запускаем control loop синхронно (вместо отдельного потока)
    try {
      entry(arg);
    } catch (const StopLoopException&) {
      // Нормальное завершение по лимиту итераций
    }
    return std::expected<void, PlatformError>{};
  }

  void DelayUntilNextTick(uint32_t period_ms) override {
    if (iteration_count_ >= max_iterations_) {
      throw StopLoopException{};
    }
    ++iteration_count_;
    AdvanceTimeMs(period_ms);
  }

  uint32_t GetIterationCount() const { return iteration_count_; }

 private:
  uint32_t max_iterations_;
  uint32_t iteration_count_{0};
};

// ══════════════════════════════════════════════════════════════════════════════
// Control Loop Integration Tests
// ══════════════════════════════════════════════════════════════════════════════

class ControlLoopTest : public ::testing::Test {
 protected:
  // Запустить control loop на N итераций, возвращая платформу для проверок.
  // IMU enabled по умолчанию (для полного покрытия pipeline).
  SimPlatform& RunLoop(uint32_t iterations, bool imu_enabled = true) {
    auto platform = std::make_unique<SimPlatform>(iterations);
    platform_ = platform.get();

    if (imu_enabled) {
      // IMU возвращает стабильные данные (стоящая машина: az≈1g)
      ImuData imu{};
      imu.az = 1.0f;
      platform_->SetImuData(imu);
    }

    vc_.SetPlatform(std::move(platform));
    (void)vc_.Init();  // Init calls CreateTask → runs loop synchronously
    return *platform_;
  }

  VehicleControlUnified vc_;
  SimPlatform* platform_{nullptr};
};

// ─────────────────────────────────────────────────────────────────────────────
// Базовые инварианты
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ControlLoopTest, LoopRunsRequestedIterations) {
  auto& sim = RunLoop(10);
  EXPECT_EQ(sim.GetIterationCount(), 10u);
}

TEST_F(ControlLoopTest, IsReadyAfterInit) {
  RunLoop(5);
  EXPECT_TRUE(vc_.IsReady());
}

// ─────────────────────────────────────────────────────────────────────────────
// Failsafe: нет сигнала → моторы в нейтрали
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ControlLoopTest, FailsafeActivated_WhenNoSignal) {
  auto& sim = RunLoop(20);
  // Ни RC, ни Wi-Fi не активны → failsafe → PWM в нейтрали
  EXPECT_FLOAT_EQ(sim.GetLastThrottle(), 0.0f);
  EXPECT_FLOAT_EQ(sim.GetLastSteering(), 0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Wi-Fi команда → PWM output
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ControlLoopTest, WifiCommand_ProducesPwmOutput) {
  auto platform = std::make_unique<SimPlatform>(50);
  platform_ = platform.get();

  ImuData imu{};
  imu.az = 1.0f;
  platform_->SetImuData(imu);
  platform_->SetWifiCommand(RcCommand{0.5f, 0.3f});

  vc_.SetPlatform(std::move(platform));
  (void)vc_.Init();

  // Throttle должен быть ненулевым (хотя slew rate замедляет нарастание)
  EXPECT_NE(platform_->GetLastThrottle(), 0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// RC приоритет над Wi-Fi
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ControlLoopTest, RcOverridesWifi) {
  auto platform = std::make_unique<SimPlatform>(100);
  platform_ = platform.get();

  ImuData imu{};
  imu.az = 1.0f;
  platform_->SetImuData(imu);
  platform_->SetRcCommand(RcCommand{0.8f, -0.2f});
  platform_->SetWifiCommand(RcCommand{0.1f, 0.1f});

  vc_.SetPlatform(std::move(platform));
  (void)vc_.Init();

  // RC throttle 0.8 >> Wi-Fi throttle 0.1 → output should trend toward 0.8
  // (slew rate needs enough iterations to ramp up)
  EXPECT_GT(platform_->GetLastThrottle(), 0.05f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Telemetry log заполняется при наличии IMU
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ControlLoopTest, TelemetryLogPopulated_WithImu) {
  RunLoop(
      200);  // 200 × 2ms = 400ms → должно быть ~4 log frames (100 Hz = 10ms)

  size_t count = 0, cap = 0;
  vc_.GetLogInfo(count, cap);
  EXPECT_GT(count, 0u);
  EXPECT_GT(cap, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Без IMU — loop работает, но log не заполняется
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ControlLoopTest, NoImu_LoopStillRuns) {
  auto platform = std::make_unique<SimPlatform>(20);
  platform_ = platform.get();
  // НЕ устанавливаем IMU data → InitImu вернёт Ok, но ReadImu = nullopt
  // Однако FakePlatform::InitImu() возвращает Ok, а ReadImu() возвращает
  // nullopt ImuHandler увидит nullopt и не включится

  vc_.SetPlatform(std::move(platform));
  (void)vc_.Init();

  EXPECT_TRUE(vc_.IsReady());
}

// ─────────────────────────────────────────────────────────────────────────────
// Config round-trip через интерфейс
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ControlLoopTest, ConfigPersistence_RoundTrip) {
  RunLoop(5);

  // First switch mode (ApplyModeDefaults applies Sport defaults)
  auto cfg = vc_.GetStabilizationConfig();
  cfg.mode = DriveMode::Sport;
  EXPECT_TRUE(vc_.SetStabilizationConfig(cfg, false));

  // Now modify PID within the same mode — no defaults override
  cfg = vc_.GetStabilizationConfig();
  EXPECT_EQ(cfg.mode, DriveMode::Sport);
  cfg.yaw_rate.pid.kp = 1.23f;
  EXPECT_TRUE(vc_.SetStabilizationConfig(cfg, true));

  auto loaded = vc_.GetStabilizationConfig();
  EXPECT_EQ(loaded.mode, DriveMode::Sport);
  EXPECT_FLOAT_EQ(loaded.yaw_rate.pid.kp, 1.23f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Kids Mode toggle
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ControlLoopTest, KidsMode_Toggle) {
  RunLoop(5);

  EXPECT_FALSE(vc_.IsKidsModeActive());
  vc_.SetKidsModeActive(true);
  EXPECT_TRUE(vc_.IsKidsModeActive());
  vc_.SetKidsModeActive(false);
  EXPECT_FALSE(vc_.IsKidsModeActive());
}

// ─────────────────────────────────────────────────────────────────────────────
// Self-test после инициализации
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ControlLoopTest, SelfTest_ReturnsResults) {
  RunLoop(10);

  auto results = vc_.RunSelfTest();
  EXPECT_FALSE(results.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// ClearLog
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ControlLoopTest, ClearLog_EmptiesBuffer) {
  RunLoop(200);

  size_t count = 0, cap = 0;
  vc_.GetLogInfo(count, cap);
  EXPECT_GT(count, 0u);

  vc_.ClearLog();
  vc_.GetLogInfo(count, cap);
  EXPECT_EQ(count, 0u);
}

// `/api/log.bin` отдаёт и кадры, и события, поэтому «Очистить лог» обязан
// чистить обе секции. Иначе события прошлых прогонов переживают очистку и
// всплывают в свежем CSV (LOS-226).
TEST_F(ControlLoopTest, ClearLog_AlsoClearsEvents) {
  RunLoop(20);

  ASSERT_TRUE(vc_.StartTest(TestParams{}));
  ASSERT_GT(vc_.GetEventCount(), 0u);

  vc_.ClearLog();
  EXPECT_EQ(vc_.GetEventCount(), 0u) << "события пережили очистку лога";
}

// ─────────────────────────────────────────────────────────────────────────────
// TestRunner (Task 6): взаимное исключение, старт/стоп
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ControlLoopTest, TestRunner_StartStop) {
  RunLoop(5);

  TestParams params;
  params.type = TestType::Straight;
  params.target_accel_g = 0.1f;
  params.duration_sec = 3.0f;
  EXPECT_TRUE(vc_.StartTest(params));
  EXPECT_TRUE(vc_.IsTestActive());

  vc_.StopTest();
  EXPECT_FALSE(vc_.IsTestActive());
}

// Auto-forward стартует не через AutoDriveCoordinator::Start*, а напрямую в
// CalibrationManager. Точка входа обязана проходить тот же гейт по пульту:
// иначе ACK рапортует ok:true, а первый же тик Update() прибивает процедуру
// абортом по RC — то есть «стартовали и молча ничего» (замечание code review
// к LOS-214).
TEST_F(ControlLoopTest, AutoForwardCalib_RejectedWhileRcActive) {
  auto platform = std::make_unique<SimPlatform>(20);
  platform_ = platform.get();

  ImuData imu{};
  imu.az = 1.0f;
  platform_->SetImuData(imu);
  // Стадия 1 пройдена — иначе старт отклонялся бы по другой причине
  // и тест проходил бы вхолостую.
  ImuCalibData calib{};
  calib.valid = true;
  platform_->SetCalibData(calib);
  platform_->SetRcCommand(RcCommand{0.0f, 0.0f});  // пульт включён

  vc_.SetPlatform(std::move(platform));
  (void)vc_.Init();

  EXPECT_FALSE(vc_.StartAutoForwardCalibration(0.1f))
      << "auto-forward стартовал при активном пульте";
}

// Положительный контроль к тесту выше: без пульта тот же вызов проходит.
// Без него первый тест зелёный и на сломанном коде.
TEST_F(ControlLoopTest, AutoForwardCalib_AcceptedWithoutRc) {
  auto platform = std::make_unique<SimPlatform>(20);
  platform_ = platform.get();

  ImuData imu{};
  imu.az = 1.0f;
  platform_->SetImuData(imu);
  ImuCalibData calib{};
  calib.valid = true;
  calib.gravity_valid = true;  // 13-й раунд: StartForwardCalibration()
                               // требует РЕАЛЬНУЮ Full-калибровку
  platform_->SetCalibData(calib);
  // RC-команду не задаём: пульт неактивен

  vc_.SetPlatform(std::move(platform));
  (void)vc_.Init();

  EXPECT_TRUE(vc_.StartAutoForwardCalibration(0.1f));
}

TEST_F(ControlLoopTest, TestRunner_MutualExclusion_WithTrimCalib) {
  RunLoop(5);

  // Start trim calibration first
  EXPECT_TRUE(vc_.StartSteeringTrimCalibration(0.1f));
  EXPECT_TRUE(vc_.IsSteeringTrimCalibActive());

  // TestRunner should be rejected while trim calib is active
  TestParams params;
  params.type = TestType::Straight;
  params.target_accel_g = 0.1f;
  params.duration_sec = 3.0f;
  EXPECT_FALSE(vc_.StartTest(params));

  vc_.StopSteeringTrimCalibration();
}

// ─────────────────────────────────────────────────────────────────────────────
// CoM Offset Calibration (Task 2): старт/стоп, взаимное исключение
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ControlLoopTest, ComOffsetCalib_StartStop) {
  RunLoop(5);

  EXPECT_TRUE(vc_.StartComOffsetCalibration(0.1f, 0.5f, 5.0f));
  EXPECT_TRUE(vc_.IsComOffsetCalibActive());

  vc_.StopComOffsetCalibration();
  EXPECT_FALSE(vc_.IsComOffsetCalibActive());
}

TEST_F(ControlLoopTest, ComOffsetCalib_MutualExclusion_WithTestRunner) {
  RunLoop(5);

  // Start test first
  TestParams params;
  params.type = TestType::Circle;
  params.target_accel_g = 0.1f;
  params.duration_sec = 3.0f;
  params.steering = 0.5f;
  EXPECT_TRUE(vc_.StartTest(params));

  // CoM calib should be rejected
  EXPECT_FALSE(vc_.StartComOffsetCalibration(0.1f, 0.5f, 5.0f));

  vc_.StopTest();
}

// ─────────────────────────────────────────────────────────────────────────────
// Steering Trim Calibration: старт/стоп через интерфейс
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ControlLoopTest, SteeringTrimCalib_StartStop) {
  RunLoop(5);

  EXPECT_TRUE(vc_.StartSteeringTrimCalibration(0.1f));
  EXPECT_TRUE(vc_.IsSteeringTrimCalibActive());

  vc_.StopSteeringTrimCalibration();
  EXPECT_FALSE(vc_.IsSteeringTrimCalibActive());
}

// ─────────────────────────────────────────────────────────────────────────────
// Отложенные команды mag-калибровки (LOS-221, ревью PR #308)
// ─────────────────────────────────────────────────────────────────────────────

// Собрать типы событий mag-калибровки из лога, в порядке появления.
static std::vector<TelemetryEventType> MagCalibEvents(
    const IVehicleControl& vc) {
  std::vector<TelemetryEventType> out;
  const size_t count = vc.GetEventCount();
  for (size_t i = 0; i < count; ++i) {
    TelemetryEvent ev{};
    if (!vc.GetEvent(i, ev)) continue;
    switch (ev.type) {
      case TelemetryEventType::MagCalibStart:
      case TelemetryEventType::MagCalibDone:
      case TelemetryEventType::MagCalibFailed:
      case TelemetryEventType::MagCalibCancelled:
        out.push_back(ev.type);
        break;
      default:
        break;
    }
  }
  return out;
}

TEST_F(ControlLoopTest, MagCalibCommandsAreDeferredToControlLoop) {
  // Команды приходят из HTTP/WS-задачи и трогают mag_calib_, madgwick_ и
  // imu_handler_ — все они принадлежат control loop и не потокобезопасны.
  // Выполнять их на месте нельзя: завершение это многошаговая
  // последовательность, между шагами которой control loop успевает сделать
  // 100 Гц чтение магнитометра со ещё СТАРЫМ offset и заново пометить семпл
  // пригодным для засева курса. Поэтому команды только ставятся в очередь, а
  // применяются первым же тиком.
  RunLoop(0);

  vc_.StartMagCalibration();
  EXPECT_STREQ(vc_.GetMagCalibStatus(), "idle")
      << "Старт обязан быть отложенным, а не применённым на месте";
  vc_.HostStep(2);
  ASSERT_STREQ(vc_.GetMagCalibStatus(), "collecting");

  vc_.FinishMagCalibration();
  EXPECT_STREQ(vc_.GetMagCalibStatus(), "collecting")
      << "Завершение обязано быть отложенным, а не применённым на месте";
  vc_.HostStep(2);
  EXPECT_STRNE(vc_.GetMagCalibStatus(), "collecting")
      << "Первый же тик control loop обязан применить отложенный запрос";
}

TEST_F(ControlLoopTest, MagCalibRequestsApplyInArrivalOrder) {
  // Пачка команд, пришедшая с WS внутри одного тика (2 мс), применяется в том
  // же порядке — наблюдаемое поведение совпадает с прежним синхронным.
  // Отдельно откладывать finish, оставив start синхронным, недостаточно:
  // control loop доходит до mag_calib_ уже вне мьютекса, и Start() успевал бы
  // в этот зазор подменить сессию под начатым завершением (ревью PR #308).
  RunLoop(0);

  vc_.StartMagCalibration();
  vc_.FinishMagCalibration();
  vc_.StartMagCalibration();
  vc_.HostStep(2);

  EXPECT_STREQ(vc_.GetMagCalibStatus(), "collecting")
      << "Последний start обязан пережить finish, поставленный до него";
  EXPECT_EQ(MagCalibEvents(vc_),
            (std::vector<TelemetryEventType>{
                TelemetryEventType::MagCalibStart,
                TelemetryEventType::MagCalibFailed,  // 0 семплов
                TelemetryEventType::MagCalibStart}));
}

TEST_F(ControlLoopTest, MagCalibCancelIsLastWhenQueuedLast) {
  // Та же пачка, но с отменой в хвосте. Статус тут не показателен (Finish()
  // отработает до Cancel(), а тот вернёт Idle), поэтому смотрим лог событий:
  // отмена обязана быть последней, а не перекрытой завершением.
  RunLoop(0);

  vc_.StartMagCalibration();
  vc_.FinishMagCalibration();
  vc_.CancelMagCalibration();
  vc_.HostStep(2);

  EXPECT_STREQ(vc_.GetMagCalibStatus(), "idle");

  const auto events = MagCalibEvents(vc_);
  ASSERT_FALSE(events.empty());
  EXPECT_EQ(events.back(), TelemetryEventType::MagCalibCancelled)
      << "Отменённая калибровка не должна досылать событие завершения после "
         "MagCalibCancelled";
}

TEST_F(ControlLoopTest, FailedMagCalibKeepsMagSampleValid) {
  // Неудачная попытка перекалибровки (мало семплов) НЕ трогает offset:
  // прежняя калибровка остаётся в силе, а значит остаётся в силе и всё, что
  // к ней сошлось. Гасить кэшированный mag-семпл в этом случае нельзя —
  // ImuHandler ушёл бы на 6DOF-путь, а тот обнуляет опору курса
  // (yaw_has_absolute_ref_ / marg_correction_progress_), и следующие 12 с
  // калибровка СК засевала бы курс по одному мгновенному семплу вместо уже
  // сошедшегося yaw — ровно тот скачок курса, который чинит LOS-221
  // (ревью PR #308).
  auto platform = std::make_unique<SimPlatform>(0);
  platform_ = platform.get();

  ImuData imu{};
  imu.az = 1.0f;
  platform_->SetImuData(imu);

  MagData mag{};
  mag.mx = 300.f;
  mag.my = 40.f;
  mag.mz = -400.f;
  platform_->SetMagData(mag);

  // Валидная калибровка «из NVS» — сценарий ревью требует, чтобы старая
  // калибровка оставалась валидной после неудачной попытки.
  MagCalibData stored{};
  stored.valid = true;
  platform_->SetStoredMagCalib(stored);

  vc_.SetPlatform(std::move(platform));
  (void)vc_.Init();  // Init вызывает CreateTask → цикл идёт синхронно
  EXPECT_STREQ(vc_.GetMagCalibStatus(), "done")
      << "Калибровка, поднятая из NVS, обязана быть видна снаружи";

  // Прогрев: магнитометр читается на 100 Гц, телеметрия публикуется на 20 Гц.
  for (int i = 0; i < 60; ++i) {
    platform_->AdvanceTimeMs(2);
    vc_.HostStep(2);
  }
  ASSERT_TRUE(platform_->GetLastSnap().mag_enabled);

  // Дальше опрос магнитометра ломаем: иначе следующее же 100 Гц чтение
  // вернуло бы mag_enabled_ в true за 10 мс и замаскировало инвалидацию.
  // Запас до kMagStaleTimeoutMs = 250 мс перекрывает интервал телеметрии.
  platform_->SetMagReadShouldFail(true);

  vc_.StartMagCalibration();
  vc_.FinishMagCalibration();  // семплов 0 < kMinSamples → Failed

  for (int i = 0; i < 30; ++i) {
    platform_->AdvanceTimeMs(2);
    vc_.HostStep(2);
  }

  ASSERT_STREQ(vc_.GetMagCalibStatus(), "failed");
  EXPECT_STREQ(vc_.GetMagCalibFailReason(), "too_few_samples")
      << "Статус и причина публикуются одним снимком — расходиться не могут";
  EXPECT_TRUE(platform_->GetLastSnap().mag_enabled)
      << "Провалившаяся перекалибровка не меняет offset — гасить mag-семпл "
         "и терять опору курса не за что";
}
