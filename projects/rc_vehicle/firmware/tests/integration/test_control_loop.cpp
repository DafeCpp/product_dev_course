#include <gtest/gtest.h>

#include <stdexcept>

#include "vehicle_control_unified.hpp"
#include "mock_platform.hpp"

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

  Result<Unit, PlatformError> CreateTask(void (*entry)(void*),
                                         void* arg) override {
    // Запускаем control loop синхронно (вместо отдельного потока)
    try {
      entry(arg);
    } catch (const StopLoopException&) {
      // Нормальное завершение по лимиту итераций
    }
    return Unit{};
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
  RunLoop(200);  // 200 × 2ms = 400ms → должно быть ~4 log frames (100 Hz = 10ms)

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
  // Однако FakePlatform::InitImu() возвращает Ok, а ReadImu() возвращает nullopt
  // ImuHandler увидит nullopt и не включится

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
