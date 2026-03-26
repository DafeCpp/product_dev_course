#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

#include "vehicle_ekf.hpp"

using namespace rc_vehicle;

// ═══════════════════════════════════════════════════════════════════════════
// Инициализация и сброс
// ═══════════════════════════════════════════════════════════════════════════

TEST(VehicleEkfTest, DefaultState_AllZero) {
  VehicleEkf ekf;
  EXPECT_FLOAT_EQ(ekf.GetVx(), 0.0f);
  EXPECT_FLOAT_EQ(ekf.GetVy(), 0.0f);
  EXPECT_FLOAT_EQ(ekf.GetYawRate(), 0.0f);
}

TEST(VehicleEkfTest, DefaultSlipAngle_Zero) {
  VehicleEkf ekf;
  EXPECT_FLOAT_EQ(ekf.GetSlipAngleDeg(), 0.0f);
}

TEST(VehicleEkfTest, DefaultSpeed_Zero) {
  VehicleEkf ekf;
  EXPECT_FLOAT_EQ(ekf.GetSpeedMs(), 0.0f);
}

TEST(VehicleEkfTest, DefaultVariance_Positive) {
  VehicleEkf ekf;
  EXPECT_GT(ekf.GetVxVariance(), 0.0f);
  EXPECT_GT(ekf.GetVyVariance(), 0.0f);
  EXPECT_GT(ekf.GetRVariance(), 0.0f);
}

TEST(VehicleEkfTest, SetState_UpdatesState) {
  VehicleEkf ekf;
  ekf.SetState(3.0f, -1.0f, 0.5f);
  EXPECT_FLOAT_EQ(ekf.GetVx(), 3.0f);
  EXPECT_FLOAT_EQ(ekf.GetVy(), -1.0f);
  EXPECT_FLOAT_EQ(ekf.GetYawRate(), 0.5f);
}

TEST(VehicleEkfTest, Reset_RestoresZeroState) {
  VehicleEkf ekf;
  ekf.SetState(5.0f, 2.0f, 1.0f);
  ekf.Predict(0.5f, 0.1f, 0.002f);
  ekf.Reset();
  EXPECT_FLOAT_EQ(ekf.GetVx(), 0.0f);
  EXPECT_FLOAT_EQ(ekf.GetVy(), 0.0f);
  EXPECT_FLOAT_EQ(ekf.GetYawRate(), 0.0f);
}

TEST(VehicleEkfTest, Reset_RestoresInitialVariance) {
  VehicleEkf ekf;
  const float p_vx_init = ekf.GetVxVariance();
  // После серии предсказаний P растёт
  for (int i = 0; i < 100; ++i) {
    ekf.Predict(0.0f, 0.0f, 0.002f);
  }
  EXPECT_GT(ekf.GetVxVariance(), p_vx_init);
  // После сброса — возвращается к начальному значению
  ekf.Reset();
  EXPECT_FLOAT_EQ(ekf.GetVxVariance(), p_vx_init);
}

// ═══════════════════════════════════════════════════════════════════════════
// Шаг предсказания (Predict)
// ═══════════════════════════════════════════════════════════════════════════

TEST(VehicleEkfTest, Predict_ZeroInput_ZeroInitial_StateStaysZero) {
  // Нет ускорений, нет вращения → состояние не меняется
  VehicleEkf ekf;
  ekf.Predict(0.0f, 0.0f, 0.002f);
  EXPECT_FLOAT_EQ(ekf.GetVx(), 0.0f);
  EXPECT_FLOAT_EQ(ekf.GetVy(), 0.0f);
  EXPECT_FLOAT_EQ(ekf.GetYawRate(), 0.0f);
}

TEST(VehicleEkfTest, Predict_LongitudinalAccel_IncreasesVx) {
  // ax = 10 м/с², dt = 0.1 с → vx = 1.0 м/с
  VehicleEkf ekf;
  ekf.Predict(10.0f, 0.0f, 0.1f);
  EXPECT_NEAR(ekf.GetVx(), 1.0f, 1e-5f);
  EXPECT_FLOAT_EQ(ekf.GetVy(), 0.0f);
}

TEST(VehicleEkfTest, Predict_LateralAccel_IncreasesVy) {
  // ay = 5 м/с², vx=0, r=0 → vy_dot = ay - r*vx = 5
  // Через dt = 0.1 с: vy = 0.5 м/с
  VehicleEkf ekf;
  ekf.Predict(0.0f, 5.0f, 0.1f);
  EXPECT_NEAR(ekf.GetVy(), 0.5f, 1e-5f);
}

TEST(VehicleEkfTest, Predict_YawRate_Unchanged) {
  // r — random walk, без измерения не меняется
  VehicleEkf ekf;
  ekf.SetState(0.0f, 0.0f, 1.5f);
  ekf.Predict(0.0f, 0.0f, 0.002f);
  EXPECT_FLOAT_EQ(ekf.GetYawRate(), 1.5f);
}

TEST(VehicleEkfTest, Predict_Coupling_RotationAffectsVx) {
  // vx=5, vy=1, r=0.5 → vx_dot = ax + r*vy = 0 + 0.5*1 = 0.5
  // Через dt = 0.1: vx = 5 + 0.1 * 0.5 = 5.05
  VehicleEkf ekf;
  ekf.SetState(5.0f, 1.0f, 0.5f);
  ekf.Predict(0.0f, 0.0f, 0.1f);
  EXPECT_NEAR(ekf.GetVx(), 5.05f, 1e-4f);
}

TEST(VehicleEkfTest, Predict_Coupling_RotationAffectsVy) {
  // vx=5, vy=0, r=1.0 → vy_dot = ay - r*vx = 0 - 1*5 = -5
  // Через dt = 0.1: vy = -0.5 (машина начинает скользить при повороте без боковой силы)
  VehicleEkf ekf;
  ekf.SetState(5.0f, 0.0f, 1.0f);
  ekf.Predict(0.0f, 0.0f, 0.1f);
  EXPECT_NEAR(ekf.GetVy(), -0.5f, 1e-4f);
}

TEST(VehicleEkfTest, Predict_ZeroDt_NoChange) {
  // dt = 0 → состояние не должно меняться
  VehicleEkf ekf;
  ekf.SetState(3.0f, 1.0f, 0.5f);
  ekf.Predict(10.0f, 5.0f, 0.0f);
  EXPECT_FLOAT_EQ(ekf.GetVx(), 3.0f);
  EXPECT_FLOAT_EQ(ekf.GetVy(), 1.0f);
}

TEST(VehicleEkfTest, Predict_IncreasesVariance) {
  // После серии предсказаний без обновлений ковариация должна расти
  VehicleEkf ekf;
  const float p_vx_init = ekf.GetVxVariance();
  for (int i = 0; i < 50; ++i) {
    ekf.Predict(0.0f, 0.0f, 0.002f);
  }
  EXPECT_GT(ekf.GetVxVariance(), p_vx_init);
}

// ═══════════════════════════════════════════════════════════════════════════
// Шаг обновления (UpdateGyroZ)
// ═══════════════════════════════════════════════════════════════════════════

TEST(VehicleEkfTest, UpdateGyroZ_PullsRateTowardMeasurement) {
  // Начальное r=0, измерение gz=1.0 → r двигается к 1.0
  VehicleEkf ekf;
  ekf.UpdateGyroZ(1.0f);
  EXPECT_GT(ekf.GetYawRate(), 0.0f);
  EXPECT_LE(ekf.GetYawRate(), 1.0f);
}

TEST(VehicleEkfTest, UpdateGyroZ_NoChangeWhenAlreadyMatch) {
  // r уже равно измерению → инновация = 0 → состояние не меняется
  VehicleEkf ekf;
  ekf.SetState(0.0f, 0.0f, 1.0f);
  ekf.UpdateGyroZ(1.0f);
  EXPECT_FLOAT_EQ(ekf.GetYawRate(), 1.0f);
}

TEST(VehicleEkfTest, UpdateGyroZ_DecreasesRVariance) {
  // После обновления P[2][2] должна уменьшиться
  VehicleEkf ekf;
  const float p_r_before = ekf.GetRVariance();
  ekf.UpdateGyroZ(0.5f);
  EXPECT_LT(ekf.GetRVariance(), p_r_before);
}

TEST(VehicleEkfTest, UpdateGyroZ_Convergence_MultipleSteps) {
  // После многократного обновления с gz=2.0 → r → 2.0
  VehicleEkf ekf;
  for (int i = 0; i < 200; ++i) {
    ekf.Predict(0.0f, 0.0f, 0.002f);
    ekf.UpdateGyroZ(2.0f);
  }
  EXPECT_NEAR(ekf.GetYawRate(), 2.0f, 0.01f);
}

TEST(VehicleEkfTest, UpdateGyroZ_NegativeMeasurement) {
  // gz = -1.5 → r становится отрицательным
  VehicleEkf ekf;
  for (int i = 0; i < 100; ++i) {
    ekf.Predict(0.0f, 0.0f, 0.002f);
    ekf.UpdateGyroZ(-1.5f);
  }
  EXPECT_NEAR(ekf.GetYawRate(), -1.5f, 0.05f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Угол заноса (slip angle)
// ═══════════════════════════════════════════════════════════════════════════

TEST(VehicleEkfTest, SlipAngle_ZeroWhenStraight) {
  // vx > 0, vy = 0 → β = 0
  VehicleEkf ekf;
  ekf.SetState(5.0f, 0.0f, 0.0f);
  EXPECT_FLOAT_EQ(ekf.GetSlipAngleDeg(), 0.0f);
}

TEST(VehicleEkfTest, SlipAngle_90Deg_WhenVxZeroAndVyPositive) {
  // vx = 0, vy > 0 → β = atan2(1, 0) = 90°, физически корректно
  VehicleEkf ekf;
  ekf.SetState(0.0f, 1.0f, 0.0f);
  EXPECT_NEAR(ekf.GetSlipAngleDeg(), 90.0f, 1e-3f);
}

TEST(VehicleEkfTest, SlipAngle_ZeroWhen_ActualReverse) {
  // vx << -kMinSpeedThreshold → явный задний ход, EKF ненадёжен → 0
  // (atan2(vy, vx<0) даёт 120–180°, что является ложным срабатыванием)
  VehicleEkf ekf;
  ekf.SetState(-3.0f, 1.0f, 0.0f);
  EXPECT_FLOAT_EQ(ekf.GetSlipAngleDeg(), 0.0f);
}

TEST(VehicleEkfTest, SlipAngle_NegativeFor_RightSideslip) {
  // vy < 0 (вправо) → β < 0
  VehicleEkf ekf;
  ekf.SetState(5.0f, -1.0f, 0.0f);
  EXPECT_LT(ekf.GetSlipAngleDeg(), 0.0f);
}

TEST(VehicleEkfTest, SlipAngle_PositiveFor_LeftSideslip) {
  // vy > 0 (влево) → β > 0
  VehicleEkf ekf;
  ekf.SetState(5.0f, 1.0f, 0.0f);
  EXPECT_GT(ekf.GetSlipAngleDeg(), 0.0f);
}

TEST(VehicleEkfTest, SlipAngle_CalculatedCorrectly) {
  // β = atan2(1, 5) ≈ 11.31°
  VehicleEkf ekf;
  ekf.SetState(5.0f, 1.0f, 0.0f);
  EXPECT_NEAR(ekf.GetSlipAngleDeg(), std::atan2(1.0f, 5.0f) * 180.0f / std::numbers::pi_v<float>,
              1e-3f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Физические сценарии
// ═══════════════════════════════════════════════════════════════════════════

TEST(VehicleEkfTest, DriftScenario_SideslipDevelopsOnIce) {
  // Машина движется вперёд (vx=5) и поворачивает влево (r=1 рад/с).
  // На льду: нет боковой силы (ay_imu ≈ 0).
  // Ожидается: vy уменьшается (занос вправо = отрицательный vy).
  VehicleEkf ekf;
  ekf.SetState(5.0f, 0.0f, 0.0f);

  // Применяем r через обновление gyro
  for (int i = 0; i < 50; ++i) {
    ekf.Predict(0.0f, 0.0f, 0.002f);
    ekf.UpdateGyroZ(1.0f);  // левый поворот, без боковой силы
  }

  // vy должен стать отрицательным (занос вправо)
  EXPECT_LT(ekf.GetVy(), 0.0f);
  // Угол заноса отрицательный
  EXPECT_LT(ekf.GetSlipAngleDeg(), 0.0f);
}

TEST(VehicleEkfTest, StraightLine_NoSideslip) {
  // Прямолинейное движение с ускорением: ay=0, gz=0, ax=2
  // vy не должна заметно нарасти (нет бокового ускорения, нет вращения)
  VehicleEkf ekf;
  for (int i = 0; i < 100; ++i) {
    ekf.Predict(2.0f, 0.0f, 0.002f);
    ekf.UpdateGyroZ(0.0f);
  }
  // vx должен вырасти
  EXPECT_GT(ekf.GetVx(), 0.0f);
  // vy должен оставаться около нуля
  EXPECT_NEAR(ekf.GetVy(), 0.0f, 0.01f);
  // Угол заноса близок к нулю
  EXPECT_NEAR(ekf.GetSlipAngleDeg(), 0.0f, 0.2f);
}

TEST(VehicleEkfTest, NormalTurn_CentripetalAccel_NoFalseSlip) {
  // Машина едет вперёд (vx=3 m/s) и поворачивает (r=1 рад/с).
  // Нормальный поворот: центростремительное ускорение ay = r * vx = 3 м/с².
  // Без заноса: vy должна оставаться около нуля (демпфирование шин).
  // До исправления (без vy damping): EKF накапливал vy из малых погрешностей
  // → ложный slip angle при нормальном повороте.
  VehicleEkf ekf;
  ekf.SetState(3.0f, 0.0f, 1.0f);

  const float vx = 3.0f;
  const float r = 1.0f;
  const float ay_centripetal = r * vx;  // = 3 м/с²

  for (int i = 0; i < 500; ++i) {  // 1 секунда при 500 Hz
    ekf.Predict(0.0f, ay_centripetal, 0.002f);
    ekf.UpdateGyroZ(r);
  }

  // vy должна оставаться близкой к нулю — нет реального заноса
  EXPECT_NEAR(ekf.GetVy(), 0.0f, 0.5f);
  // Угол заноса должен быть небольшим (не детектируется как занос)
  EXPECT_LT(std::abs(ekf.GetSlipAngleDeg()), 10.0f);
}

TEST(VehicleEkfTest, Speed_IsNonNegative) {
  VehicleEkf ekf;
  ekf.SetState(-3.0f, 2.0f, 0.5f);
  EXPECT_GE(ekf.GetSpeedMs(), 0.0f);
}

TEST(VehicleEkfTest, Speed_CalculatedCorrectly) {
  VehicleEkf ekf;
  ekf.SetState(3.0f, 4.0f, 0.0f);
  EXPECT_NEAR(ekf.GetSpeedMs(), 5.0f, 1e-5f);  // sqrt(9+16) = 5
}

// ═══════════════════════════════════════════════════════════════════════════
// Параметры шума
// ═══════════════════════════════════════════════════════════════════════════

TEST(VehicleEkfTest, HighMeasurementNoise_SlowerConvergence) {
  // При высоком шуме измерения — медленнее сходится
  VehicleEkf::NoiseParams low_noise{0.1f, 0.1f, 0.05f, 0.001f};
  VehicleEkf::NoiseParams high_noise{0.1f, 0.1f, 0.05f, 10.0f};

  VehicleEkf ekf_low(low_noise);
  VehicleEkf ekf_high(high_noise);

  for (int i = 0; i < 50; ++i) {
    ekf_low.Predict(0.0f, 0.0f, 0.002f);
    ekf_low.UpdateGyroZ(1.0f);
    ekf_high.Predict(0.0f, 0.0f, 0.002f);
    ekf_high.UpdateGyroZ(1.0f);
  }

  // При низком шуме измерения — ближе к реальному gz
  EXPECT_GT(ekf_low.GetYawRate(), ekf_high.GetYawRate());
}

// ═══════════════════════════════════════════════════════════════════════════
// Zero Velocity Update (ZUPT)
// ═══════════════════════════════════════════════════════════════════════════

TEST(VehicleEkfTest, ZUPT_PullsVelocityToZero) {
  // vx=2, vy=1 → после серии ZUPT → vx≈0, vy≈0
  VehicleEkf ekf;
  ekf.SetState(2.0f, 1.0f, 0.5f);
  for (int i = 0; i < 50; ++i) {
    ekf.Predict(0.0f, 0.0f, 0.002f);
    ekf.UpdateZeroVelocity(0.1f);
  }
  EXPECT_NEAR(ekf.GetVx(), 0.0f, 0.05f);
  EXPECT_NEAR(ekf.GetVy(), 0.0f, 0.05f);
}

TEST(VehicleEkfTest, ZUPT_DoesNotAffectYawRate) {
  // ZUPT обнуляет vx,vy но yaw rate корректируется только через gyro
  VehicleEkf ekf;
  ekf.SetState(0.0f, 0.0f, 2.0f);
  for (int i = 0; i < 20; ++i) {
    ekf.Predict(0.0f, 0.0f, 0.002f);
    ekf.UpdateGyroZ(2.0f);
    ekf.UpdateZeroVelocity(0.1f);
  }
  // yaw rate должен оставаться около 2.0 — ZUPT его не обнуляет
  EXPECT_NEAR(ekf.GetYawRate(), 2.0f, 0.1f);
}

TEST(VehicleEkfTest, ZUPT_ReducesVariance) {
  // После ZUPT дисперсии vx и vy должны уменьшиться
  VehicleEkf ekf;
  // Нарастить дисперсию серией предсказаний
  for (int i = 0; i < 100; ++i) {
    ekf.Predict(1.0f, 0.5f, 0.002f);
  }
  const float pvx_before = ekf.GetVxVariance();
  const float pvy_before = ekf.GetVyVariance();
  ekf.UpdateZeroVelocity(0.1f);
  EXPECT_LT(ekf.GetVxVariance(), pvx_before);
  EXPECT_LT(ekf.GetVyVariance(), pvy_before);
}

TEST(VehicleEkfTest, ZUPT_PreventsStationaryDrift) {
  // Машина стоит (ax=0,ay=0,gz=0), но EKF без ZUPT может дрейфовать.
  // С ZUPT — vx,vy должны оставаться ≈ 0 даже через 1000 шагов.
  VehicleEkf ekf;
  for (int i = 0; i < 1000; ++i) {
    ekf.Predict(0.0f, 0.0f, 0.002f);
    ekf.UpdateGyroZ(0.0f);
    ekf.UpdateZeroVelocity(0.1f);
  }
  EXPECT_NEAR(ekf.GetVx(), 0.0f, 0.01f);
  EXPECT_NEAR(ekf.GetVy(), 0.0f, 0.01f);
  EXPECT_NEAR(ekf.GetSlipAngleDeg(), 0.0f, 0.1f);
}

TEST(VehicleEkfTest, SetNoiseParams_AffectsConvergence) {
  VehicleEkf ekf;
  // Установить очень маленький шум измерения → быстрая сходимость
  ekf.SetNoiseParams({0.1f, 0.1f, 0.05f, 0.0001f});
  for (int i = 0; i < 20; ++i) {
    ekf.Predict(0.0f, 0.0f, 0.002f);
    ekf.UpdateGyroZ(3.0f);
  }
  EXPECT_NEAR(ekf.GetYawRate(), 3.0f, 0.05f);
}
