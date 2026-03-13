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

    guard_.Init(cfg_, ekf_, &imu_handler_);
    guard_.Reset();
  }

  // Имитирует несколько итераций guard с заданными состояниями EKF
  // и проверяет IsActive()
  bool RunWithEkfState(float vx, float vy, float yaw_rate_rad,
                       int iterations = 10) {
    ekf_.SetState(vx, vy, yaw_rate_rad);
    float throttle = 1.0f;
    for (int i = 0; i < iterations; ++i) {
      guard_.Process(throttle, 2);  // dt = 2 ms (500 Hz)
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

// ══════════════════════════════════════════════════════════════════════════════
// Тест: при реальном занос (высокий yaw rate + большой slip) срабатывает
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(OversteerGuardTest, Triggers_WhenHighYawRate_AndHighSlip) {
  // Первая итерация: устанавливаем базовое состояние (нет jump в slip_rate)
  ekf_.SetState(5.0f, 0.0f, 1.0f);
  float throttle = 1.0f;
  guard_.Process(throttle, 2);

  // Резкое боковое скольжение: vx=5, vy=2.5 → slip ≈ 26.5°
  // yaw_rate = 1.0 рад/с > kMinYawRateRad
  ekf_.SetState(5.0f, 2.5f, 1.0f);

  // Прогоняем несколько итераций — slip_rate должен превысить 50°/с
  bool triggered = false;
  for (int i = 0; i < 20; ++i) {
    guard_.Process(throttle, 2);
    if (guard_.IsActive()) {
      triggered = true;
      break;
    }
  }
  EXPECT_TRUE(triggered)
      << "Занос должен срабатывать при значимом yaw_rate и большом slip angle";
}

// ══════════════════════════════════════════════════════════════════════════════
// Тест: при warn_enabled=false guard всегда молчит
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(OversteerGuardTest, NoTrigger_WhenWarnDisabled) {
  cfg_.oversteer.warn_enabled = false;
  guard_.Init(cfg_, ekf_, &imu_handler_);
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
  guard_.Process(throttle, 2);
  ekf_.SetState(5.0f, 2.5f, 1.0f);
  for (int i = 0; i < 20; ++i) {
    guard_.Process(throttle, 2);
  }

  guard_.Reset();
  EXPECT_FALSE(guard_.IsActive()) << "Reset() должен сбрасывать oversteer_active_";
}
