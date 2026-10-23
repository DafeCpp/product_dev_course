#include <gtest/gtest.h>

#include "control_components.hpp"
#include "imu_calibration.hpp"
#include "madgwick_filter.hpp"
#include "mock_platform.hpp"
#include "stabilization_config.hpp"
#include "stabilization_pipeline.hpp"
#include "vehicle_ekf.hpp"

using namespace rc_vehicle;
using namespace rc_vehicle::testing;

// ══════════════════════════════════════════════════════════════════════════════
// Вспомогательный класс: настраивает OversteerGuard с warn_enabled=true
// ══════════════════════════════════════════════════════════════════════════════

class OversteerGuardTest : public ::testing::Test {
 protected:
  void SetUp() override {
    imu_handler_.SetEnabled(true);

    cfg_.oversteer.warn_enabled = true;
    cfg_.oversteer.slip_thresh_deg = 20.0f;
    cfg_.oversteer.rate_thresh_deg_s = 50.0f;
    cfg_.oversteer.throttle_reduction = 0.5f;

    guard_.Init(ekf_, &imu_handler_);
    guard_.Reset();
  }

  // Имитирует несколько итераций guard с заданными состояниями EKF
  // и проверяет IsActive()
  bool RunWithEkfState(float vx, float vy, float yaw_rate_rad,
                       int iterations = 10) {
    ekf_.SetState(vx, vy, yaw_rate_rad);
    float throttle = 1.0f;
    for (int i = 0; i < iterations; ++i) {
      guard_.Process(cfg_, throttle, 2);  // dt = 2 ms (500 Hz)
    }
    return guard_.IsActive();
  }

  FakePlatform platform_;
  ImuCalibration calib_;
  MadgwickFilter filter_;
  ImuHandler imu_handler_{platform_, calib_, filter_};

  StabilizationConfig cfg_;
  VehicleEkf ekf_;
  OversteerGuard guard_;
};

// ══════════════════════════════════════════════════════════════════════════════
// Тест: при неподвижности (yaw_rate ≈ 0) занос не срабатывает,
// даже если EKF-дрейф даёт большой slip angle
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(OversteerGuardTest, NoTrigger_WhenStationary_ZeroYawRate) {
  // EKF задрейфовал: vx=0.5 vy=0.5 → slip=45°, но машинка стоит (gz≈0)
  EXPECT_FALSE(RunWithEkfState(0.5f, 0.5f, 0.0f))
      << "Занос не должен срабатывать при нулевой угловой скорости";
}

TEST_F(OversteerGuardTest, NoTrigger_WhenYawRateBelowThreshold) {
  // Небольшая угловая скорость — меньше kMinYawRateRad (0.3 рад/с)
  EXPECT_FALSE(RunWithEkfState(0.5f, 0.5f, 0.1f))
      << "Занос не должен срабатывать при yaw_rate < 0.3 рад/с";
}

TEST_F(OversteerGuardTest, NoTrigger_WhenSpeedBelowThreshold) {
  // EKF speed < 0.5 m/s → slip angle ненадёжен (шум vx/vy)
  // vx=0.3, vy=0.3 → speed≈0.42, slip=45°, yaw_rate=1.0 рад/с
  EXPECT_FALSE(RunWithEkfState(0.3f, 0.3f, 1.0f))
      << "Занос не должен срабатывать при speed < 0.5 м/с";
}

// ══════════════════════════════════════════════════════════════════════════════
// Тест: при реальном занос (высокий yaw rate + большой slip) срабатывает
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(OversteerGuardTest, Triggers_WhenHighYawRate_AndHighSlip) {
  // Первая итерация: устанавливаем базовое состояние (нет jump в slip_rate)
  ekf_.SetState(5.0f, 0.0f, 1.0f);
  float throttle = 1.0f;
  guard_.Process(cfg_, throttle, 2);

  // Резкое боковое скольжение: vx=5, vy=2.5 → slip ≈ 26.5°
  // yaw_rate = 1.0 рад/с > kMinYawRateRad
  ekf_.SetState(5.0f, 2.5f, 1.0f);

  // Прогоняем несколько итераций — slip_rate должен превысить 50°/с
  bool triggered = false;
  for (int i = 0; i < 20; ++i) {
    guard_.Process(cfg_, throttle, 2);
    if (guard_.IsActive()) {
      triggered = true;
      break;
    }
  }
  EXPECT_TRUE(triggered)
      << "Занос должен срабатывать при значимом yaw_rate и большом slip angle";
}

// ══════════════════════════════════════════════════════════════════════════════
// Тесты: реактивация после простоя не даёт ложного всплеска slip_rate (FW-R2)
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(OversteerGuardTest, NoTrigger_OnReactivation_WhenSlipRateLow) {
  // Фаза 1: медленная езда (speed < 0.5 м/с) с уже большим slip ≈ 21.8°
  // (atan2(0.12, 0.3)). Guard в idle-ветке, prev_slip_deg_ отслеживает slip.
  RunWithEkfState(0.3f, 0.12f, 1.0f, 10);
  ASSERT_FALSE(guard_.IsActive());

  // Фаза 2: скорость выросла, slip тот же (atan2(0.8, 2.0) ≈ 21.8°).
  // slip > slip_thresh (20°), но реальный slip_rate ≈ 0 < 50°/с.
  // До фикса prev_slip_deg_ обнулялся в idle-ветке → на первом тике
  // slip_rate = 21.8/0.002 ≈ 10900°/с → ложное срабатывание.
  ekf_.SetState(2.0f, 0.8f, 1.0f);
  float throttle = 1.0f;
  guard_.Process(cfg_, throttle, 2);
  EXPECT_FALSE(guard_.IsActive())
      << "Реактивация без реального роста slip не должна давать занос";
}

TEST_F(OversteerGuardTest, Triggers_OnRealSlipJumpAfterReactivation) {
  // Фаза 1: медленная езда без заноса (slip ≈ 0)
  RunWithEkfState(0.4f, 0.0f, 1.0f, 10);
  ASSERT_FALSE(guard_.IsActive());

  // Фаза 2: резкий реальный занос — slip скачком 0 → 26.5°
  ekf_.SetState(5.0f, 2.5f, 1.0f);
  float throttle = 1.0f;
  bool triggered = false;
  for (int i = 0; i < 20; ++i) {
    guard_.Process(cfg_, throttle, 2);
    if (guard_.IsActive()) {
      triggered = true;
      break;
    }
  }
  EXPECT_TRUE(triggered)
      << "Реальный быстрый рост slip после простоя должен детектироваться";
}

// ══════════════════════════════════════════════════════════════════════════════
// Тест: при warn_enabled=false guard всегда молчит
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(OversteerGuardTest, NoTrigger_WhenWarnDisabled) {
  cfg_.oversteer.warn_enabled = false;
  guard_.Init(ekf_, &imu_handler_);
  EXPECT_FALSE(RunWithEkfState(5.0f, 5.0f, 2.0f))
      << "При warn_enabled=false занос не должен срабатывать никогда";
}

// ══════════════════════════════════════════════════════════════════════════════
// Тест: Reset() сбрасывает активный занос
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(OversteerGuardTest, Reset_ClearsActiveFlag) {
  // Вызвать занос
  ekf_.SetState(5.0f, 0.0f, 1.0f);
  float throttle = 1.0f;
  guard_.Process(cfg_, throttle, 2);
  ekf_.SetState(5.0f, 2.5f, 1.0f);
  for (int i = 0; i < 20; ++i) {
    guard_.Process(cfg_, throttle, 2);
  }

  guard_.Reset();
  EXPECT_FALSE(guard_.IsActive()) << "Reset() должен сбрасывать oversteer_active_";
}

// ══════════════════════════════════════════════════════════════════════════════
// LOS-380: занос по вращению быстрее траектории (r > ay·g/vx) ловится раньше
// порога slip и держится до успокоения
// ══════════════════════════════════════════════════════════════════════════════

class RotationExcessTest : public ::testing::Test {
 protected:
  RotationExcessTest() {
    cfg_.oversteer.warn_enabled = true;
    cfg_.oversteer.slip_thresh_deg = 5.0f;
    cfg_.oversteer.rate_thresh_deg_s = 10.0f;
    cfg_.oversteer.throttle_reduction = 0.7f;
  }

  // Траектория поворачивает со скоростью trajectory_rps, кузов — yaw_rps.
  static StabilizationInput Corner(float yaw_rps, float trajectory_rps,
                                   float vx = 3.0f) {
    return {
        .dt_ms = 2,
        .speed_ms = 3.0f,
        .vx_ms = vx,
        .slip_angle_deg = 1.0f,
        .yaw_rate_rps = yaw_rps,
        .lateral_accel_g = trajectory_rps * vx / 9.80665f,
        .lateral_accel_valid = true,
        .imu_enabled = true,
    };
  }

  float Run(const StabilizationInput& input, int ticks, float stick = 1.0f) {
    float throttle = stick;
    for (int i = 0; i < ticks; ++i) {
      throttle = stick;
      guard_.Process(cfg_, throttle, input, true);
    }
    return throttle;
  }

  StabilizationConfig cfg_;
  OversteerGuard guard_;
};

TEST_F(RotationExcessTest, TriggersBeforeSlipThreshold) {
  // Устойчивый поворот: вращение совпадает с траекторией.
  EXPECT_FLOAT_EQ(Run(Corner(1.0f, 1.0f), 50), 1.0f);
  EXPECT_FALSE(guard_.IsActive());
  // Кузов вращается на 0.6 рад/с быстрее траектории при slip 1°.
  EXPECT_NEAR(Run(Corner(1.6f, 1.0f), 20), 0.3f, 1e-5f);
  EXPECT_TRUE(guard_.IsActive());
  EXPECT_TRUE(guard_.IsCuttingThrottle());
}

TEST_F(RotationExcessTest, IgnoresSingleTickSpike) {
  (void)Run(Corner(1.6f, 1.0f), 5);
  (void)Run(Corner(1.0f, 1.0f), 1);
  EXPECT_FALSE(guard_.IsActive());
}

TEST_F(RotationExcessTest, LongTickIsOneSample) {
  // Тик 50 мс после зависания цикла — всё равно один отсчёт.
  StabilizationInput input = Corner(1.6f, 1.0f);
  input.dt_ms = 50;
  (void)Run(input, 1);
  EXPECT_FALSE(guard_.IsActive());
  (void)Run(input, 2);
  EXPECT_TRUE(guard_.IsActive());
}

TEST_F(RotationExcessTest, LongTickDoesNotReleaseOrRestoreThrottle) {
  (void)Run(Corner(1.6f, 1.0f), 20);
  StabilizationInput calm = Corner(1.0f, 1.0f);
  calm.dt_ms = 500;
  EXPECT_NEAR(Run(calm, 1), 0.3f, 1e-5f);
  EXPECT_TRUE(guard_.IsActive());
  (void)Run(Corner(0.0f, 0.0f), 1);
  ASSERT_FALSE(guard_.IsActive());
  StabilizationInput straight = Corner(0.0f, 0.0f);
  straight.dt_ms = 3000;
  EXPECT_LT(Run(straight, 1), 0.31f);
}

TEST_F(RotationExcessTest, IgnoredWithUntrustedSpeed) {
  StabilizationInput input = Corner(1.6f, 1.0f);
  input.vx_variance = 100.0f;
  (void)Run(input, 100);
  EXPECT_FALSE(guard_.IsActive());
  input.vx_variance = 0.0f;
  input.ekf_diverged = true;
  (void)Run(input, 100);
  EXPECT_FALSE(guard_.IsActive());
}

TEST_F(RotationExcessTest, UndersteerIsNotOversteer) {
  // Снос передней оси: кузов вращается медленнее траектории.
  (void)Run(Corner(1.0f, 1.8f), 100);
  EXPECT_FALSE(guard_.IsActive());
  (void)Run(Corner(-1.0f, -1.8f), 100);
  EXPECT_FALSE(guard_.IsActive());
}

TEST_F(RotationExcessTest, IgnoredWithoutLongitudinalSpeed) {
  // Реальный лог 2026-08-02: vx по EKF ≈ 0, ay шумит — ay/vx не траектория.
  (void)Run(Corner(1.6f, 0.0f, 0.1f), 100);
  EXPECT_FALSE(guard_.IsActive());
}

TEST_F(RotationExcessTest, IgnoredWithoutTrustedTilt) {
  // Без TiltEstimator гравитацию из ay снимал бы Madgwick, а он в повороте
  // принимает боковое ускорение за крен.
  StabilizationInput input = Corner(1.6f, 1.0f);
  input.lateral_accel_valid = false;
  (void)Run(input, 100);
  EXPECT_FALSE(guard_.IsActive());
}

TEST_F(RotationExcessTest, DisabledGuardDropsCeiling) {
  (void)Run(Corner(1.6f, 1.0f), 20);
  ASSERT_TRUE(guard_.IsCuttingThrottle());
  cfg_.oversteer.warn_enabled = false;
  EXPECT_FLOAT_EQ(Run(Corner(1.6f, 1.0f), 1), 1.0f);
  EXPECT_FALSE(guard_.IsActive());
  // Включённый снова гард не возвращает старый потолок.
  cfg_.oversteer.warn_enabled = true;
  EXPECT_FLOAT_EQ(Run(Corner(0.0f, 0.0f), 1), 1.0f);
}

TEST_F(RotationExcessTest, DisabledReductionDropsCeiling) {
  (void)Run(Corner(1.6f, 1.0f), 20);
  (void)Run(Corner(0.0f, 0.0f), 1);
  ASSERT_LT(Run(Corner(0.0f, 0.0f), 1), 1.0f);
  cfg_.oversteer.throttle_reduction = 0.0f;
  EXPECT_FLOAT_EQ(Run(Corner(0.0f, 0.0f), 1), 1.0f);
  cfg_.oversteer.throttle_reduction = 0.7f;
  EXPECT_FLOAT_EQ(Run(Corner(0.0f, 0.0f), 1), 1.0f);
}

TEST_F(RotationExcessTest, DetectsWithoutReduction) {
  // Срез выключен, но предупреждение в телеметрии остаётся.
  cfg_.oversteer.throttle_reduction = 0.0f;
  EXPECT_FLOAT_EQ(Run(Corner(1.6f, 1.0f), 20), 1.0f);
  EXPECT_TRUE(guard_.IsActive());
  EXPECT_FALSE(guard_.IsCuttingThrottle());
}

TEST_F(RotationExcessTest, ImuOffDropsCeiling) {
  (void)Run(Corner(1.6f, 1.0f), 20);
  StabilizationInput input = Corner(0.0f, 0.0f);
  input.imu_enabled = false;
  (void)Run(input, 1);
  EXPECT_FLOAT_EQ(Run(Corner(0.0f, 0.0f), 1), 1.0f);
}

TEST_F(RotationExcessTest, HoldsCutUntilCalm) {
  (void)Run(Corner(1.6f, 1.0f), 20);
  ASSERT_TRUE(guard_.IsActive());
  // Вращение успокоилось, но 0.1 с мало: газ остаётся срезанным.
  EXPECT_NEAR(Run(Corner(1.0f, 1.0f), 50), 0.3f, 1e-5f);
  EXPECT_TRUE(guard_.IsActive());
  // Всплеск вращения перезапускает отсчёт спокойствия.
  (void)Run(Corner(1.6f, 1.0f), 20);
  (void)Run(Corner(1.0f, 1.0f), 50);
  EXPECT_TRUE(guard_.IsActive());
  (void)Run(Corner(1.0f, 1.0f), 60);
  EXPECT_FALSE(guard_.IsActive());
  EXPECT_FALSE(guard_.IsCuttingThrottle());
}

TEST_F(RotationExcessTest, LostRotationDataIsNotCalm) {
  (void)Run(Corner(1.6f, 1.0f), 20);
  StabilizationInput lost = Corner(1.6f, 1.0f);
  lost.lateral_accel_valid = false;
  EXPECT_NEAR(Run(lost, 200), 0.3f, 1e-5f);
  EXPECT_TRUE(guard_.IsActive());
}

TEST_F(RotationExcessTest, ReleasesAtOnceWhenRotationStops) {
  (void)Run(Corner(1.6f, 1.0f), 20);
  ASSERT_TRUE(guard_.IsActive());
  (void)Run(Corner(0.0f, 0.0f), 1);
  EXPECT_FALSE(guard_.IsActive());
}

TEST_F(RotationExcessTest, ThrottleRecoversGraduallyAfterRelease) {
  (void)Run(Corner(1.6f, 1.0f), 20);
  (void)Run(Corner(0.0f, 0.0f), 1);
  ASSERT_FALSE(guard_.IsActive());
  // Потолок растёт от среза 0.3 на 0.25/с: через 0.4 с ≈ 0.4.
  EXPECT_NEAR(Run(Corner(0.0f, 0.0f), 200), 0.4f, 0.01f);
  // Ниже потолка ручка проходит как есть.
  EXPECT_FLOAT_EQ(Run(Corner(0.0f, 0.0f), 1, 0.2f), 0.2f);
  // Через ещё 3 с потолок снят.
  EXPECT_FLOAT_EQ(Run(Corner(0.0f, 0.0f), 1500), 1.0f);
}

TEST_F(RotationExcessTest, ResetClearsCeiling) {
  (void)Run(Corner(1.6f, 1.0f), 20);
  guard_.Reset();
  EXPECT_FLOAT_EQ(Run(Corner(0.0f, 0.0f), 1), 1.0f);
}
