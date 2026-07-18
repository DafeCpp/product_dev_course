// Продольное ускорение и вектор «вперёд» (регресс LOS-214).
//
// GetForwardAccel проецировал ПОЛНЫЙ вектор акселерометра на accel_forward_vec,
// не вычитая гравитацию. При этом сама Forward-калибровка гравитацию вычитала —
// определения расходились. Любой завал оси «вперёд» превращался в постоянный
// офсет: наклон на 2° даёт ~0.03g, чего хватало на ложный отрыв в MotionDriver
// и на постоянное срезание газа в Kids Mode.
//
// Худший случай: Forward-калибровка прошла до Full, с дефолтным
// gravity_vec = {0,0,1}, а реальное az ≈ −1. Тогда «вперёд» уходит вертикально
// вниз и офсет достигает 1g.

#include <gtest/gtest.h>

#include <cmath>

#include "imu_calibration.hpp"

namespace rc_vehicle {
namespace {

// ═══════════════════════════════════════════════════════════════════════════
// Fixture
// ═══════════════════════════════════════════════════════════════════════════

class ImuCalibrationForwardTest : public ::testing::Test {
 protected:
  ImuCalibration calib;

  /** Загрузить калибровку с заданными «вперёд» и «вниз». */
  void Load(float fx, float fy, float fz, float gx = 0.f, float gy = 0.f,
            float gz = 1.f) {
    ImuCalibData d{};
    d.valid = true;
    d.gravity_vec[0] = gx;
    d.gravity_vec[1] = gy;
    d.gravity_vec[2] = gz;
    d.accel_forward_vec[0] = fx;
    d.accel_forward_vec[1] = fy;
    d.accel_forward_vec[2] = fz;
    calib.SetData(d);
  }

  static ImuData Accel(float ax, float ay, float az) {
    ImuData d{};
    d.ax = ax;
    d.ay = ay;
    d.az = az;
    return d;
  }
};

// ═══════════════════════════════════════════════════════════════════════════
// Покой → ноль
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(ImuCalibrationForwardTest, AtRest_LevelVehicle_ReturnsZero) {
  Load(1.f, 0.f, 0.f);
  EXPECT_NEAR(calib.GetForwardAccel(Accel(0.f, 0.f, 1.f)), 0.0f, 1e-5f);
}

TEST_F(ImuCalibrationForwardTest, Forward_Acceleration_Measured) {
  Load(1.f, 0.f, 0.f);
  EXPECT_NEAR(calib.GetForwardAccel(Accel(0.2f, 0.f, 1.f)), 0.2f, 1e-5f);
}

TEST_F(ImuCalibrationForwardTest, Backward_Acceleration_IsNegative) {
  Load(1.f, 0.f, 0.f);
  EXPECT_NEAR(calib.GetForwardAccel(Accel(-0.15f, 0.f, 1.f)), -0.15f, 1e-5f);
}

// Ключевой регресс: завал оси «вперёд» не должен давать офсет в покое.
TEST_F(ImuCalibrationForwardTest, TiltedForwardVector_AtRest_StillZero) {
  Load(1.f, 0.f, 0.2f);  // ~11° завала вниз
  EXPECT_NEAR(calib.GetForwardAccel(Accel(0.f, 0.f, 1.f)), 0.0f, 1e-5f);
}

TEST_F(ImuCalibrationForwardTest, TiltedForwardVector_IsOrthogonalized) {
  Load(1.f, 0.f, 0.2f);
  const auto& d = calib.GetData();
  EXPECT_NEAR(d.accel_forward_vec[2], 0.0f, 1e-5f);
  EXPECT_NEAR(d.accel_forward_vec[0], 1.0f, 1e-5f);
  EXPECT_TRUE(d.forward_valid);
}

// Инвертированный монтаж IMU: gravity ≈ {0,0,−1}, «вперёд» по-прежнему по X.
TEST_F(ImuCalibrationForwardTest, InvertedGravity_AtRest_ReturnsZero) {
  Load(1.f, 0.f, 0.f, 0.f, 0.f, -1.f);
  EXPECT_NEAR(calib.GetForwardAccel(Accel(0.f, 0.f, -1.f)), 0.0f, 1e-5f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Валидация испорченного вектора
// ═══════════════════════════════════════════════════════════════════════════

// Худший случай из LOS-214: «вперёд» смотрит вертикально вниз.
TEST_F(ImuCalibrationForwardTest, VerticalForwardVector_Rejected) {
  Load(0.f, 0.f, -1.f, 0.f, 0.f, 1.f);

  const auto& d = calib.GetData();
  EXPECT_FALSE(d.forward_valid) << "вертикальная ось «вперёд» принята";
  EXPECT_NEAR(d.accel_forward_vec[0], 1.0f, 1e-5f);
  EXPECT_NEAR(d.accel_forward_vec[2], 0.0f, 1e-5f);
}

TEST_F(ImuCalibrationForwardTest, VerticalForwardVector_NoRestingOffset) {
  Load(0.f, 0.f, -1.f, 0.f, 0.f, 1.f);
  // До правки здесь было бы ≈ +1g — источник ложного отрыва.
  EXPECT_NEAR(calib.GetForwardAccel(Accel(0.f, 0.f, 1.f)), 0.0f, 1e-5f);
}

TEST_F(ImuCalibrationForwardTest, MildTilt_Accepted_NotRejected) {
  Load(1.f, 0.f, 0.1f);  // ~6°, в пределах kMaxForwardTilt
  EXPECT_TRUE(calib.GetData().forward_valid);
}

// ═══════════════════════════════════════════════════════════════════════════
// SetForwardDirection (ручная команда по WS)
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(ImuCalibrationForwardTest, SetForwardDirection_Orthogonalizes) {
  Load(1.f, 0.f, 0.f);
  calib.SetForwardDirection(0.f, 1.f, 0.5f);

  const auto& d = calib.GetData();
  EXPECT_NEAR(d.accel_forward_vec[2], 0.0f, 1e-5f);
  EXPECT_NEAR(d.accel_forward_vec[1], 1.0f, 1e-5f);
  EXPECT_NEAR(calib.GetForwardAccel(Accel(0.f, 0.f, 1.f)), 0.0f, 1e-5f);
}

TEST_F(ImuCalibrationForwardTest, SetForwardDirection_AlongGravity_Ignored) {
  Load(1.f, 0.f, 0.f);
  calib.SetForwardDirection(0.f, 0.f, 1.f);  // вдоль гравитации

  const auto& d = calib.GetData();
  EXPECT_FALSE(d.forward_valid);
  EXPECT_NEAR(d.accel_forward_vec[0], 1.0f, 1e-5f);  // прежний сохранён
}

// ═══════════════════════════════════════════════════════════════════════════
// Forward-калибровка целиком
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(ImuCalibrationForwardTest, ForwardCalibration_RecoversHorizontalAxis) {
  Load(1.f, 0.f, 0.f);
  ASSERT_TRUE(calib.StartForwardCalibration(10));

  // Разгон вдоль +Y при гравитации по +Z
  for (int i = 0; i < 10; ++i) {
    calib.FeedSample(Accel(0.f, 0.3f, 1.f));
  }

  ASSERT_EQ(calib.GetStatus(), CalibStatus::Done);
  const auto& d = calib.GetData();
  EXPECT_NEAR(d.accel_forward_vec[1], 1.0f, 1e-4f);
  EXPECT_NEAR(d.accel_forward_vec[2], 0.0f, 1e-4f);
  EXPECT_TRUE(d.forward_valid);
}

// Движение только по вертикали не задаёт ось «вперёд» — калибровка обязана
// провалиться, а не записать мусор.
TEST_F(ImuCalibrationForwardTest, ForwardCalibration_VerticalOnly_Fails) {
  Load(1.f, 0.f, 0.f);
  ASSERT_TRUE(calib.StartForwardCalibration(10));

  for (int i = 0; i < 10; ++i) {
    calib.FeedSample(Accel(0.f, 0.f, 1.3f));  // «ускорение» вдоль g
  }

  EXPECT_EQ(calib.GetStatus(), CalibStatus::Failed);
}

}  // namespace
}  // namespace rc_vehicle
