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
// Согласованность вертикали с Apply() (замечание code review к LOS-214)
//
// Full-калибровка кладёт в accel_bias ВСЮ статику осей X/Y, включая проекцию
// гравитации при наклонном монтаже. Поэтому после Apply() покой равен
// (0,0,±1), а gravity_vec остаётся сырым наклонённым вектором. Если вычитать
// gravity_vec из bias-скорректированных данных, в покое появляется офсет
// −gravity_vec[0] — до правки здесь было −0.139g при наклоне 8°.
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(ImuCalibrationForwardTest,
       FullCalibOnTiltedMount_RestForwardAccelIsZero) {
  constexpr float kPitch = 8.0f * 3.14159265f / 180.0f;
  const float rest_ax = std::sin(kPitch);
  const float rest_az = std::cos(kPitch);

  calib.StartCalibration(CalibMode::Full, 10);
  for (int i = 0; i < 10; ++i) {
    calib.FeedSample(Accel(rest_ax, 0.f, rest_az));
  }
  ASSERT_EQ(calib.GetStatus(), CalibStatus::Done);

  // Как в control loop: сначала Apply(), потом GetForwardAccel()
  ImuData rest = Accel(rest_ax, 0.f, rest_az);
  calib.Apply(rest);

  EXPECT_NEAR(calib.GetForwardAccel(rest), 0.0f, 1e-4f)
      << "статический офсет в покое после Full-калибровки на наклоне";
}

// Разгон машины ВПЕРЁД глазами наклонённого датчика: вектор ускорения
// повёрнут вместе с монтажом, а не направлен вдоль оси X датчика.
// Именно эта деталь была упущена в первой версии теста, из-за чего занижение
// продольного ускорения прошло незамеченным.
static ImuData TiltedRest(float pitch_rad) {
  ImuData d{};
  d.ax = std::sin(pitch_rad);
  d.az = std::cos(pitch_rad);
  return d;
}

static ImuData TiltedForwardAccel(float pitch_rad, float accel_g) {
  // Корпус: (accel_g, 0, 1) → СК датчика поворотом на pitch вокруг Y
  ImuData d{};
  d.ax = std::cos(pitch_rad) * accel_g + std::sin(pitch_rad);
  d.az = -std::sin(pitch_rad) * accel_g + std::cos(pitch_rad);
  return d;
}

TEST_F(ImuCalibrationForwardTest, FullCalibOnTiltedMount_ForwardAccelMeasured) {
  constexpr float kPitch = 8.0f * 3.14159265f / 180.0f;

  calib.StartCalibration(CalibMode::Full, 10);
  for (int i = 0; i < 10; ++i) {
    calib.FeedSample(TiltedRest(kPitch));
  }
  ASSERT_EQ(calib.GetStatus(), CalibStatus::Done);

  // Стадия 2: учим ось «вперёд» на реальном разгоне
  ASSERT_TRUE(calib.StartForwardCalibration(10));
  for (int i = 0; i < 10; ++i) {
    calib.FeedSample(TiltedForwardAccel(kPitch, 0.3f));
  }
  ASSERT_EQ(calib.GetStatus(), CalibStatus::Done);

  ImuData moving = TiltedForwardAccel(kPitch, 0.2f);
  calib.Apply(moving);
  EXPECT_NEAR(calib.GetForwardAccel(moving), 0.2f, 1e-3f);
}

// Ортогонализация не должна срезать компоненту угла монтажа: при опоре на
// (0,0,±1) вместо gravity_vec ось «вперёд» уплощается и продольное ускорение
// занижается в cos(угла).
//
// Угол взят близким к предельно поддерживаемому. Потолок задан
// kMaxAccelBias = 0.5g: при наклоне монтажа θ в accel_bias попадает sin(θ),
// поэтому монтаж круче asin(0.5) = 30° не переживает перезагрузку — см.
// SteepMount_BeyondBiasLimit_DoesNotSurviveReload ниже.
TEST_F(ImuCalibrationForwardTest, SteepMount_ForwardAccelNotUnderReported) {
  constexpr float kPitch = 25.0f * 3.14159265f / 180.0f;

  calib.StartCalibration(CalibMode::Full, 10);
  for (int i = 0; i < 10; ++i) {
    calib.FeedSample(TiltedRest(kPitch));
  }
  ASSERT_EQ(calib.GetStatus(), CalibStatus::Done);

  ASSERT_TRUE(calib.StartForwardCalibration(10));
  for (int i = 0; i < 10; ++i) {
    calib.FeedSample(TiltedForwardAccel(kPitch, 0.3f));
  }
  ASSERT_EQ(calib.GetStatus(), CalibStatus::Done);

  // Выученная ось обязана сохранить наклон монтажа
  const auto& d = calib.GetData();
  EXPECT_NEAR(std::abs(d.accel_forward_vec[2]), std::sin(kPitch), 1e-3f)
      << "компонента угла монтажа срезана ортогонализацией";

  ImuData moving = TiltedForwardAccel(kPitch, 0.2f);
  calib.Apply(moving);
  EXPECT_NEAR(calib.GetForwardAccel(moving), 0.2f, 2e-3f);

  // И покой по-прежнему ноль
  ImuData rest = TiltedRest(kPitch);
  calib.Apply(rest);
  EXPECT_NEAR(calib.GetForwardAccel(rest), 0.0f, 1e-3f);

  // Калибровка обязана пережить перезагрузку: сохранение в NVS и загрузка
  // обратно идут через SetData(), которая валидирует accel_bias.
  ImuCalibration reloaded;
  reloaded.SetData(calib.GetData());
  ASSERT_TRUE(reloaded.IsValid()) << "калибровка отброшена при перезагрузке";
  EXPECT_TRUE(reloaded.GetData().forward_valid);

  ImuData moving2 = TiltedForwardAccel(kPitch, 0.2f);
  reloaded.Apply(moving2);
  EXPECT_NEAR(reloaded.GetForwardAccel(moving2), 0.2f, 2e-3f)
      << "после перезагрузки ось «вперёд» потеряна";
}

// Характеризует текущий предел: монтаж круче ~30° кладёт в accel_bias больше
// kMaxAccelBias, и SetData() отбрасывает калибровку целиком. В памяти она
// работает, но перезагрузку не переживает, и авто-манёвры молча уезжают на
// дефолтную ось X. Тест фиксирует границу, чтобы она не была сюрпризом;
// снятие ограничения — отдельная задача (guard не различает смещение датчика
// и наклон монтажа).
TEST_F(ImuCalibrationForwardTest,
       SteepMount_BeyondBiasLimit_DoesNotSurviveReload) {
  constexpr float kPitch = 45.0f * 3.14159265f / 180.0f;

  calib.StartCalibration(CalibMode::Full, 10);
  for (int i = 0; i < 10; ++i) {
    calib.FeedSample(TiltedRest(kPitch));
  }
  ASSERT_EQ(calib.GetStatus(), CalibStatus::Done);
  ASSERT_GT(std::abs(calib.GetData().accel_bias[0]),
            ImuCalibration::kMaxAccelBias);

  ImuCalibration reloaded;
  reloaded.SetData(calib.GetData());
  EXPECT_FALSE(reloaded.IsValid())
      << "предел kMaxAccelBias изменился — обновить документацию границы";
}

// ═══════════════════════════════════════════════════════════════════════════
// Отмена сбора (замечание code review к LOS-214)
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(ImuCalibrationForwardTest, CancelCalibration_StopsCollecting) {
  Load(1.f, 0.f, 0.f);
  ASSERT_TRUE(calib.StartForwardCalibration(10));
  ASSERT_EQ(calib.GetStatus(), CalibStatus::Collecting);

  calib.CancelCalibration();
  EXPECT_EQ(calib.GetStatus(), CalibStatus::Failed);

  // Дальнейшие семплы не должны довести сбор до Done и перезаписать ось.
  // Разгон вдоль +Y: если бы сбор продолжался, «вперёд» стало бы (0,1,0).
  for (int i = 0; i < 50; ++i) {
    calib.FeedSample(Accel(0.f, 0.3f, 1.f));
  }
  EXPECT_EQ(calib.GetStatus(), CalibStatus::Failed);
  EXPECT_NEAR(calib.GetData().accel_forward_vec[0], 1.0f, 1e-5f)
      << "ось «вперёд» перезаписана отменённым сбором";
  EXPECT_NEAR(calib.GetData().accel_forward_vec[1], 0.0f, 1e-5f);
}

TEST_F(ImuCalibrationForwardTest, CancelCalibration_DoesNotClobberDone) {
  Load(1.f, 0.f, 0.f);
  ASSERT_TRUE(calib.StartForwardCalibration(10));
  for (int i = 0; i < 10; ++i) {
    calib.FeedSample(Accel(0.f, 0.3f, 1.f));
  }
  ASSERT_EQ(calib.GetStatus(), CalibStatus::Done);

  calib.CancelCalibration();  // no-op: сбор уже завершён
  EXPECT_EQ(calib.GetStatus(), CalibStatus::Done);
}

// ═══════════════════════════════════════════════════════════════════════════
// RotateToVehicleFrame (код-ревью PR #290, LOS-240)
//
// TiltEstimator, в отличие от Madgwick (SetVehicleFrame корректирует вывод),
// сам не знает про наклонный монтаж. При наклоне IMU относительно корпуса
// bias-corrected ax/ay/gx/gy остаются смесью осей ДАТЧИКА, а не корпуса — их
// нужно повернуть в СК машины ПЕРЕД TiltEstimator::Update(), иначе тангаж на
// наклонном монтаже получается систематически неверным (P1 code review).
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(ImuCalibrationForwardTest,
       RotateToVehicleFrame_IdentityWhenUncalibrated) {
  // Дефолт: gravity_vec=(0,0,1), accel_forward_vec=(1,0,0) — тождественная СК.
  ImuData d{0.2f, 0.1f, 0.9f, 5.f, -3.f, 1.f};
  calib.RotateToVehicleFrame(d);
  EXPECT_NEAR(d.ax, 0.2f, 1e-5f);
  EXPECT_NEAR(d.ay, 0.1f, 1e-5f);
  EXPECT_NEAR(d.az, 0.9f, 1e-5f);
  EXPECT_NEAR(d.gx, 5.f, 1e-5f);
  EXPECT_NEAR(d.gy, -3.f, 1e-5f);
  EXPECT_NEAR(d.gz, 1.f, 1e-5f);
}

// ВАЖНО: RotateToVehicleFrame() рассчитана на данные ПОСЛЕ Apply() (как их
// получает control_loop_processor.cpp через sensors_.imu_data). Apply()
// сдвигает начало отсчёта (bias), но не поворачивает оси, и accel_bias
// подобран Finalize() так, что покой после Apply() — ВСЕГДА (0,0,±1),
// независимо от наклона монтажа (см. GetForwardAccel() выше). Поэтому тесты
// прогоняют реальную калибровку (Full+Forward) и Apply(), а не задают
// gravity_vec/accel_forward_vec напрямую через Load() с нулевым bias — иначе
// «наклонный монтаж» на входе RotateToVehicleFrame() был бы нереалистичен
// (P1, код-ревью PR #290: до фикса гравитация проецировалась дважды — и
// через down-поправку в Apply(), и повторно через сырой наклонённый базис).

TEST_F(ImuCalibrationForwardTest,
       RotateToVehicleFrame_TiltedMount_RestAccelIsUp) {
  constexpr float kPitch = 20.0f * 3.14159265f / 180.0f;
  calib.StartCalibration(CalibMode::Full, 10);
  for (int i = 0; i < 10; ++i) calib.FeedSample(TiltedRest(kPitch));
  ASSERT_EQ(calib.GetStatus(), CalibStatus::Done);
  ASSERT_TRUE(calib.StartForwardCalibration(10));
  for (int i = 0; i < 10; ++i) {
    calib.FeedSample(TiltedForwardAccel(kPitch, 0.3f));
  }
  ASSERT_EQ(calib.GetStatus(), CalibStatus::Done);

  // В покое на ровной машине СК-машины ожидание: ax=0, ay=0, az=1 —
  // независимо от угла монтажа.
  ImuData rest = TiltedRest(kPitch);
  calib.Apply(rest);
  calib.RotateToVehicleFrame(rest);
  EXPECT_NEAR(rest.ax, 0.0f, 1e-3f);
  EXPECT_NEAR(rest.ay, 0.0f, 1e-3f);
  EXPECT_NEAR(rest.az, 1.0f, 1e-3f);
}

TEST_F(ImuCalibrationForwardTest,
       RotateToVehicleFrame_TiltedMount_ForwardAccelMatchesVehicleFrame) {
  // Прямолинейный разгон 0.2g на ровном месте, наклонный монтаж 20°: после
  // ротации продольное ускорение читается по оси X СК машины, боковое/
  // вертикальное — без примеси. Это то, что раньше проецировал вручную
  // GetForwardAccel(); теперь TiltEstimator получает то же самое как полный
  // 3-осевой accel/gyro вектор.
  constexpr float kPitch = 20.0f * 3.14159265f / 180.0f;
  calib.StartCalibration(CalibMode::Full, 10);
  for (int i = 0; i < 10; ++i) calib.FeedSample(TiltedRest(kPitch));
  ASSERT_EQ(calib.GetStatus(), CalibStatus::Done);
  ASSERT_TRUE(calib.StartForwardCalibration(10));
  for (int i = 0; i < 10; ++i) {
    calib.FeedSample(TiltedForwardAccel(kPitch, 0.3f));
  }
  ASSERT_EQ(calib.GetStatus(), CalibStatus::Done);

  ImuData moving = TiltedForwardAccel(kPitch, 0.2f);
  calib.Apply(moving);
  calib.RotateToVehicleFrame(moving);
  EXPECT_NEAR(moving.ax, 0.2f, 3e-3f);
  EXPECT_NEAR(moving.ay, 0.0f, 1e-3f);
  EXPECT_NEAR(moving.az, 1.0f, 3e-3f);
}

TEST_F(ImuCalibrationForwardTest,
       RotateToVehicleFrame_FullCalibOnly_OrthogonalizesDefaultForward) {
  // После ТОЛЬКО Full-калибровки (до Forward) accel_forward_vec остаётся
  // дефолтным (1,0,0), forward_valid=false — не ортогонален наклонённому
  // gravity_vec. RotateToVehicleFrame обязана переортогонализовать базис
  // сама (как MadgwickFilter::SetVehicleFrame()), а не давать перекошенный
  // результат (P2, код-ревью PR #290).
  constexpr float kPitch = 20.0f * 3.14159265f / 180.0f;
  calib.StartCalibration(CalibMode::Full, 10);
  for (int i = 0; i < 10; ++i) calib.FeedSample(TiltedRest(kPitch));
  ASSERT_EQ(calib.GetStatus(), CalibStatus::Done);
  ASSERT_FALSE(calib.GetData().forward_valid);

  ImuData rest = TiltedRest(kPitch);
  calib.Apply(rest);
  calib.RotateToVehicleFrame(rest);
  EXPECT_NEAR(rest.ax, 0.0f, 1e-2f);
  EXPECT_NEAR(rest.ay, 0.0f, 1e-2f);
  EXPECT_NEAR(rest.az, 1.0f, 1e-2f);
}

TEST_F(ImuCalibrationForwardTest,
       RotateToVehicleFrame_InvertedMount_RestAccelIsCanonicalUp) {
  // Перевёрнутый монтаж: gravity_vec[2]<0 (см. InvertedGravity_AtRest_
  // ReturnsZero выше — уже поддерживаемый случай). «Уровень» в СК машины
  // обязан читаться как (0,0,+1) — КАНОНИЧЕСКИ, а не (0,0,-1) — иначе
  // TiltEstimator::Update()'s atan2(ay,az) даёт atan2(0,-1)=π вместо 0
  // (P1, код-ревью PR #290, 4-й раунд).
  Load(1.f, 0.f, 0.f, 0.f, 0.f, -1.f);

  ImuData rest{0.f, 0.f, -1.f, 0.f, 0.f, 0.f};  // сырой покой на инвертире
  calib.RotateToVehicleFrame(rest);
  EXPECT_NEAR(rest.ax, 0.0f, 1e-5f);
  EXPECT_NEAR(rest.ay, 0.0f, 1e-5f);
  EXPECT_NEAR(rest.az, 1.0f, 1e-5f);
}

TEST_F(ImuCalibrationForwardTest,
       RotateToVehicleFrame_InvertedMount_ForwardAccelPreserved) {
  // На перевёрнутом монтаже продольное ускорение по-прежнему должно
  // корректно читаться по оси X СК машины, а az — оставаться каноническим
  // (0,0,+1) при разгоне на ровном месте.
  Load(1.f, 0.f, 0.f, 0.f, 0.f, -1.f);

  ImuData moving{0.2f, 0.f, -1.f, 0.f, 0.f, 0.f};
  calib.RotateToVehicleFrame(moving);
  EXPECT_NEAR(moving.ax, 0.2f, 1e-5f);
  EXPECT_NEAR(moving.ay, 0.0f, 1e-5f);
  EXPECT_NEAR(moving.az, 1.0f, 1e-5f);
}

TEST_F(ImuCalibrationForwardTest,
       RotateToVehicleFrame_DegenerateBasis_LeavesDataUnchanged) {
  // «Вперёд» вдоль гравитации — построить базис нельзя. Не должно портить
  // данные (ни NaN, ни произвольный поворот) — фолбэк на тождественное
  // преобразование, как при отсутствии калибровки.
  Load(0.f, 0.f, 1.f, 0.f, 0.f, 1.f);
  ImuData d{0.1f, 0.2f, 0.3f, 1.f, 2.f, 3.f};
  calib.RotateToVehicleFrame(d);
  EXPECT_NEAR(d.ax, 0.1f, 1e-5f);
  EXPECT_NEAR(d.ay, 0.2f, 1e-5f);
  EXPECT_NEAR(d.az, 0.3f, 1e-5f);
  EXPECT_NEAR(d.gx, 1.f, 1e-5f);
  EXPECT_NEAR(d.gy, 2.f, 1e-5f);
  EXPECT_NEAR(d.gz, 3.f, 1e-5f);
}

// Код-ревью PR #290 (8-й раунд): проверка знака Y_veh против конвенции,
// уже установленной в VehicleEkf (vy>0/gz>0 = «влево», ay=+r·vx для левого
// поворота — см. VehicleEkfTest.NormalTurn_CentripetalAccel_NoFalseSlip/
// SlipAngle_PositiveFor_LeftSideslip/DriftScenario_SideslipDevelopsOnIce в
// test_vehicle_ekf.cpp). Y_veh = Z_veh×X_veh — прямая проверка построением:
// «право» = поворот X_veh на −90° вокруг Z_veh (по часовой сверху), значит
// Y_veh обязана быть «лево». Раскладка через явную геометрию, а не только
// абстрактную алгебру — чтобы не полагаться на комментарий в коде (который
// как раз был перепутан: «Y_veh (вправо)» при формуле, дающей «влево»).
TEST_F(ImuCalibrationForwardTest,
       RotateToVehicleFrame_YAxisMatchesEkfLeftPositiveConvention) {
  // Монтаж на 90° по yaw (как TiltComp_YawedMount в
  // test_control_loop_processor.cpp): «вперёд» машины — сенсорная Y.
  Load(0.f, 1.f, 0.f,   // accel_forward_vec (X_veh) = сенсорная Y
       0.f, 0.f, 1.f);  // gravity_vec (Z_veh) = сенсорная Z (без наклона)

  // Истинное центростремительное ускорение при ЛЕВОМ повороте на этом
  // монтаже физически направлено вдоль сенсорной −X (право = X_veh,
  // повёрнутая на −90° вокруг Z_veh = сенсорная +X для X_veh=сенсорная Y).
  constexpr float kCentripetalG = 0.3f;
  ImuData d{-kCentripetalG, 0.f, 1.f, 0.f, 0.f, 0.f};
  calib.RotateToVehicleFrame(d);

  // rotated ay обязана быть ПОЛОЖИТЕЛЬНОЙ (влево), совпадая с конвенцией
  // ay=+r·vx для левого поворота, установленной в VehicleEkf.
  EXPECT_NEAR(d.ay, kCentripetalG, 1e-5f)
      << "Y_veh не согласована с left-positive конвенцией EKF";
  EXPECT_NEAR(d.ax, 0.0f, 1e-5f);
}

TEST_F(ImuCalibrationForwardTest, RotateToVehicleFrame_RotatesGyroToo) {
  // Чистое вращение по тангажу корпуса (вокруг Y машины) на наклонном
  // монтаже должно после ротации читаться целиком по gy СК машины, без
  // утечки в gx — иначе гиро-интеграция TiltEstimator накопит перекрёстную
  // ошибку между pitch и roll.
  constexpr float kPitch = 20.0f * 3.14159265f / 180.0f;
  Load(std::cos(kPitch), 0.f, -std::sin(kPitch), std::sin(kPitch), 0.f,
       std::cos(kPitch));

  // Угловая скорость корпуса (0, 50, 0) dps в СК машины повёрнута в СК
  // датчика тем же поворотом на kPitch вокруг Y — Y инвариантна.
  ImuData d{};
  d.gx = 0.f;
  d.gy = 50.f;
  d.gz = 0.f;
  calib.RotateToVehicleFrame(d);
  EXPECT_NEAR(d.gx, 0.0f, 1e-3f);
  EXPECT_NEAR(d.gy, 50.0f, 1e-3f);
  EXPECT_NEAR(d.gz, 0.0f, 1e-3f);
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
