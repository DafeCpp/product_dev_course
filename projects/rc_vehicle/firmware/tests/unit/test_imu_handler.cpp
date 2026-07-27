// Регресс LOS-229 (review r3629660991): ImuHandler::UpdateMagAndHeading
// выставлял mag_enabled_ = true при первом успешном чтении магнетометра и
// никогда не сбрасывал его обратно, если последующие чтения начинали
// проваливаться. FeedMadgwick в этом случае продолжал кормить
// MadgwickFilter::UpdateWithMag замороженным (последним успешным)
// mag_calibrated_ сколько угодно долго — сам фильтр не может определить,
// что семпл устарел. Если калибровка завершалась во время такого простоя,
// SetVehicleFrame() мог закрепить курс, посчитанный по неактуальному полю,
// вместо честного отката в 6DOF.

#include <gtest/gtest.h>

#include "control_components.hpp"
#include "imu_calibration.hpp"
#include "madgwick_filter.hpp"
#include "mock_platform.hpp"

namespace rc_vehicle {
namespace {

using testing::FakePlatform;

ImuData LevelImu() {
  ImuData d{};
  d.ax = 0.f;
  d.ay = 0.f;
  d.az = -1.f;
  d.gx = d.gy = d.gz = 0.f;
  return d;
}

MagData SomeMag() { return MagData{0.0f, 0.6f, -0.8f}; }

TEST(ImuFrameTest, NormalizeMountedImuToVehicleFrame) {
  ImuData data{};
  data.ax = -0.2f;  // физическое ускорение вперёд на установленном IMU
  data.ay = 0.1f;
  data.az = -1.0f;
  data.gx = 1.0f;
  data.gy = 2.0f;
  data.gz = -30.0f;  // левый поворот по установленному IMU

  NormalizeMountedImuToVehicleFrame(data);

  EXPECT_NEAR(data.ax, 0.2f, 1e-6f);
  EXPECT_NEAR(data.ay, 0.1f, 1e-6f);
  EXPECT_NEAR(data.az, -1.0f, 1e-6f);
  EXPECT_NEAR(data.gx, 1.0f, 1e-6f);
  EXPECT_NEAR(data.gy, 2.0f, 1e-6f);
  EXPECT_NEAR(data.gz, 30.0f, 1e-6f);
}

TEST(ImuHandlerTest, MagEnabledStaysTrueThroughBriefReadBlip) {
  // Кратковременный сбой (один пропуск) — не должен считаться поломкой
  // датчика: mag_enabled_ остаётся true.
  FakePlatform platform;
  ImuCalibration calib;
  MadgwickFilter filter;
  ImuHandler imu(platform, calib, filter, /*read_interval_ms=*/2);
  imu.SetEnabled(true);
  platform.SetImuData(LevelImu());
  platform.SetMagData(SomeMag());

  uint32_t now_ms = 10;
  imu.Update(now_ms, 10);
  ASSERT_TRUE(imu.IsMagEnabled())
      << "Первое успешное чтение должно включить mag";

  // Один пропуск (< kMagStaleTimeoutMs = 250 мс от последнего успеха).
  platform.SetMagReadShouldFail(true);
  now_ms += 10;
  imu.Update(now_ms, 10);
  EXPECT_TRUE(imu.IsMagEnabled())
      << "Кратковременный сбой не должен отключать магнетометр";

  // Восстановление
  platform.SetMagReadShouldFail(false);
  now_ms += 10;
  imu.Update(now_ms, 10);
  EXPECT_TRUE(imu.IsMagEnabled());
}

TEST(ImuHandlerTest, MagDisabledAfterProlongedReadFailure) {
  // Ревью PR #283 (r3629660991): устойчивый сбой чтения дольше
  // kMagStaleTimeoutMs должен сбросить mag_enabled_ — иначе FeedMadgwick
  // кормит фильтр замороженными данными неограниченно долго.
  FakePlatform platform;
  ImuCalibration calib;
  MadgwickFilter filter;
  ImuHandler imu(platform, calib, filter, /*read_interval_ms=*/2);
  imu.SetEnabled(true);
  platform.SetImuData(LevelImu());
  platform.SetMagData(SomeMag());

  uint32_t now_ms = 10;
  imu.Update(now_ms, 10);
  ASSERT_TRUE(imu.IsMagEnabled());

  platform.SetMagReadShouldFail(true);
  // Шагаем по 10 мс (частота опроса магнетометра) до заведомого превышения
  // таймаута в 250 мс от последнего успешного чтения.
  for (int i = 0; i < 40; ++i) {
    now_ms += 10;
    imu.Update(now_ms, 10);
  }
  EXPECT_FALSE(imu.IsMagEnabled())
      << "Затянувшийся сбой чтения должен откатить в 6DOF";
}

TEST(ImuHandlerTest,
     StaleMagDropoutDoesNotPreserveYawOnRecalibrationAfterwards) {
  // Полный сценарий из ревью: курс сходится по магнитометру, датчик
  // отказывает, машина в это время предположительно поворачивается — если
  // бы mag_enabled_ оставался true, повторная калибровка сохранила бы
  // устаревший курс вместо честного обнуления (LOS-229).
  FakePlatform platform;
  ImuCalibration calib;
  MadgwickFilter filter;
  ImuHandler imu(platform, calib, filter, /*read_interval_ms=*/2);
  imu.SetEnabled(true);
  platform.SetImuData(LevelImu());
  platform.SetMagData(SomeMag());

  ImuCalibData valid_calib{};
  valid_calib.valid = true;
  valid_calib.gravity_vec[0] = 0.f;
  valid_calib.gravity_vec[1] = 0.f;
  valid_calib.gravity_vec[2] = -1.f;
  valid_calib.accel_forward_vec[0] = 1.f;
  valid_calib.accel_forward_vec[1] = 0.f;
  valid_calib.accel_forward_vec[2] = 0.f;
  calib.SetData(valid_calib);

  uint32_t now_ms = 0;
  // Первая калибровка (машина стоит ровно) — устанавливает vehicle frame.
  // Магнетометр опрашивается на 100 Гц, первое чтение случится не раньше
  // 10 мс — mag_enabled_ проверяем уже после цикла сходимости ниже.
  now_ms += 2;
  imu.Update(now_ms, 2);

  // Даём фильтру достаточно времени сойтись по магнитометру (500 Гц, 14 с —
  // с запасом над kMinMargProgressForYawRef при дефолтном beta=0.1).
  for (int i = 0; i < 7000; ++i) {
    now_ms += 2;
    imu.Update(now_ms, 2);
  }
  ASSERT_TRUE(imu.IsMagEnabled());
  float pitch, roll, yaw_converged;
  filter.GetEulerDeg(pitch, roll, yaw_converged);
  ASSERT_GT(std::abs(yaw_converged), 5.0f)
      << "Тест бессмысленен, если курс не сошёлся к ненулевому значению";

  // Магнетометр перестаёт отвечать дольше таймаута.
  platform.SetMagReadShouldFail(true);
  for (int i = 0; i < 150; ++i) {  // 150 * 2 мс = 300 мс > kMagStaleTimeoutMs
    now_ms += 2;
    imu.Update(now_ms, 2);
  }
  ASSERT_FALSE(imu.IsMagEnabled())
      << "К этому моменту магнетометр должен быть уже отключён";

  // Повторная калибровка (машина по-прежнему физически стоит ровно):
  // сброс валидности и повторная установка — имитирует реальный цикл
  // ImuCalibDone.
  calib.SetData(ImuCalibData{});
  now_ms += 2;
  imu.Update(now_ms, 2);
  calib.SetData(valid_calib);
  now_ms += 2;
  imu.Update(now_ms, 2);

  float pitch_after, roll_after, yaw_after;
  filter.GetEulerDeg(pitch_after, roll_after, yaw_after);
  EXPECT_NEAR(yaw_after, 0.0f, 0.1f)
      << "После затянувшегося простоя магнетометра курс не подкреплён "
         "актуальными данными — калибровка должна обнулить его, а не "
         "закрепить устаревшее значение";
  EXPECT_NEAR(pitch_after, 0.0f, 0.1f);
  EXPECT_NEAR(roll_after, 0.0f, 0.1f);
}

TEST(ImuHandlerTest, CalibrationOnExactStaleTimeoutTickDoesNotPreserveYaw) {
  // Ревью PR #283 (r3629768456): UpdateVehicleFrame() вызывался ДО
  // UpdateMagAndHeading()/FeedMadgwick(), поэтому на тике, где калибровка
  // становится валидной РОВНО в момент истечения таймаута устаревшего
  // магнетометра, SetVehicleFrame() читал filter_.yaw_has_absolute_ref_ ещё
  // с предыдущего тика — mag_enabled_ на тот момент ещё не был
  // инвалидирован, и курс, посчитанный по уже замороженному mag-семплу, мог
  // закрепиться вместо честного обнуления.
  FakePlatform platform;
  ImuCalibration calib;  // остаётся невалидной до точного момента ниже
  MadgwickFilter filter;
  ImuHandler imu(platform, calib, filter, /*read_interval_ms=*/2);
  imu.SetEnabled(true);
  platform.SetImuData(LevelImu());
  platform.SetMagData(SomeMag());

  uint32_t now_ms = 0;
  // Сходимся по магнитометру, пока калибровка невалидна (vehicle frame не
  // трогаем, чтобы не создавать лишних побочных вызовов SetVehicleFrame).
  for (int i = 0; i < 7000; ++i) {  // 7000 * 2 мс = 14 с
    now_ms += 2;
    imu.Update(now_ms, 2);
  }
  ASSERT_EQ(now_ms, 14000u);
  ASSERT_TRUE(imu.IsMagEnabled());
  float pitch, roll, yaw_converged;
  filter.GetEulerDeg(pitch, roll, yaw_converged);
  ASSERT_GT(std::abs(yaw_converged), 5.0f)
      << "Тест бессмысленен, если курс не сошёлся к ненулевому значению";

  // Последнее успешное чтение mag было на границе 14000 мс. Магнетометр
  // перестаёт отвечать; шагаем ДО, но не ВКЛЮЧАЯ тик, на котором истекает
  // kMagStaleTimeoutMs. Таймаут проверяется на КАЖДОМ IMU-тике (2 мс), а не
  // только на 10-мс опросах магнетометра (review r3629933116, LOS-229) —
  // 14250 даёт ровно 250 (не > порога), 14252 — первый тик, где 252 > 250.
  platform.SetMagReadShouldFail(true);
  while (now_ms < 14250) {
    now_ms += 2;
    imu.Update(now_ms, 2);
  }
  ASSERT_EQ(now_ms, 14250u);
  ASSERT_TRUE(imu.IsMagEnabled())
      << "На этом тике таймаут ещё не должен был сработать (250, не > 250)";

  // Критический тик: калибровка становится валидной РОВНО на том же тике,
  // где магнетометр впервые признаётся устаревшим (252 мс > 250).
  ImuCalibData valid_calib{};
  valid_calib.valid = true;
  valid_calib.gravity_vec[0] = 0.f;
  valid_calib.gravity_vec[1] = 0.f;
  valid_calib.gravity_vec[2] = -1.f;
  valid_calib.accel_forward_vec[0] = 1.f;
  valid_calib.accel_forward_vec[1] = 0.f;
  valid_calib.accel_forward_vec[2] = 0.f;
  calib.SetData(valid_calib);
  now_ms += 2;
  ASSERT_EQ(now_ms, 14252u);
  imu.Update(now_ms, 2);

  EXPECT_FALSE(imu.IsMagEnabled())
      << "На этом тике магнетометр должен был уже признаться устаревшим";

  float pitch_after, roll_after, yaw_after;
  filter.GetEulerDeg(pitch_after, roll_after, yaw_after);
  EXPECT_NEAR(yaw_after, 0.0f, 0.1f)
      << "Калибровка на том же тике, где магнетометр признан устаревшим, "
         "не должна закрепить курс, посчитанный по замороженному семплу";
  EXPECT_NEAR(pitch_after, 0.0f, 0.1f);
  EXPECT_NEAR(roll_after, 0.0f, 0.1f);
}

namespace {

ImuCalibData LevelCalib() {
  ImuCalibData d{};
  d.valid = true;
  d.gravity_vec[0] = 0.f;
  d.gravity_vec[1] = 0.f;
  d.gravity_vec[2] = -1.f;
  d.accel_forward_vec[0] = 1.f;
  d.accel_forward_vec[1] = 0.f;
  d.accel_forward_vec[2] = 0.f;
  return d;
}

// Прогнать хэндлер до момента сразу ПОСЛЕ 100 Гц чтения магнетометра, чтобы
// следующее чтение отстояло ровно на kMagReadIntervalMs.
uint32_t RunUntilFreshMagRead(ImuHandler& imu) {
  uint32_t now_ms = 0;
  for (int i = 0; i < 7000; ++i) {  // 7000 * 2 мс = 14 с
    now_ms += 2;
    imu.Update(now_ms, 2);
  }
  return now_ms;  // 14000 — кратно 10 мс, значит чтение только что прошло
}

}  // namespace

TEST(ImuHandlerTest, MagRecalibrationBlocksYawSeedUntilFreshSample) {
  // Ревью PR #308 (LOS-221): FinishMagCalibration() зовёт
  // MadgwickFilter::InvalidateYawTrust(), но ImuHandler отдаёт кэшированный
  // mag_calibrated_ в UpdateWithMag() на КАЖДОМ тике, обновляя его лишь на
  // 100 Гц чтениях. Первый же тик после инвалидации снова помечал этот СТАРЫЙ
  // вектор пригодным для засева, и калибровка, попавшая в окно до следующего
  // чтения (≤10 мс), закрепила бы курс по ровно той mag-калибровке, которую
  // только что объявили негодной. InvalidateMagSample() закрывает это окно.
  FakePlatform platform;
  ImuCalibration calib;
  MadgwickFilter filter;
  ImuHandler imu(platform, calib, filter, /*read_interval_ms=*/2);
  imu.SetEnabled(true);
  platform.SetImuData(LevelImu());
  platform.SetMagData(SomeMag());

  uint32_t now_ms = RunUntilFreshMagRead(imu);
  ASSERT_EQ(now_ms, 14000u);
  ASSERT_TRUE(imu.IsMagEnabled());

  // Смена mag-калибровки в том же порядке, что и в FinishMagCalibration():
  // сначала гасится кэшированный семпл (до Finish(), пока старый offset ещё
  // активен), и лишь потом сбрасывается опора курса в фильтре. Одного
  // InvalidateYawTrust() мало: он гасит кэш засева, но следующий же тик
  // восстановил бы его по старому вектору.
  imu.InvalidateMagSample();
  filter.InvalidateYawTrust();
  EXPECT_FALSE(imu.IsMagEnabled());

  // Калибровка попадает в окно ДО следующего чтения (2 мс из 10).
  calib.SetData(LevelCalib());
  now_ms += 2;
  imu.Update(now_ms, 2);
  ASSERT_EQ(now_ms, 14002u);
  EXPECT_FALSE(imu.IsMagEnabled())
      << "До свежего чтения магнетометр не должен считаться пригодным";

  float pitch, roll, yaw;
  filter.GetEulerDeg(pitch, roll, yaw);
  EXPECT_NEAR(yaw, 0.0f, 0.1f)
      << "Курс не должен засеваться по вектору от старой mag-калибровки";
  EXPECT_NEAR(pitch, 0.0f, 0.1f);
  EXPECT_NEAR(roll, 0.0f, 0.1f);
}

TEST(ImuHandlerTest, YawSeedResumesAfterFreshMagSample) {
  // Обратная сторона предыдущего теста: блокировка снимается сама, как только
  // приходит чтение, уже пересчитанное новой калибровкой (≤10 мс). Иначе
  // InvalidateMagSample() выключал бы засев навсегда.
  FakePlatform platform;
  ImuCalibration calib;
  MadgwickFilter filter;
  ImuHandler imu(platform, calib, filter, /*read_interval_ms=*/2);
  imu.SetEnabled(true);
  platform.SetImuData(LevelImu());
  platform.SetMagData(SomeMag());

  uint32_t now_ms = RunUntilFreshMagRead(imu);
  imu.InvalidateMagSample();
  ASSERT_FALSE(imu.IsMagEnabled());

  // Доходим до следующего 100 Гц чтения — оно уже свежее.
  while (now_ms < 14010) {
    now_ms += 2;
    imu.Update(now_ms, 2);
  }
  ASSERT_TRUE(imu.IsMagEnabled())
      << "Свежее чтение обязано вернуть магнетометр в строй";

  calib.SetData(LevelCalib());
  now_ms += 2;
  imu.Update(now_ms, 2);

  float pitch, roll, yaw;
  filter.GetEulerDeg(pitch, roll, yaw);
  // mag=(0, 0.6, -0.8) при gravity=(0,0,-1), forward=(1,0,0) →
  // ψ=atan2(my,mx)=90°.
  EXPECT_NEAR(std::abs(yaw), 90.0f, 0.5f)
      << "После свежего семпла засев должен снова работать";
  EXPECT_NEAR(pitch, 0.0f, 0.1f);
  EXPECT_NEAR(roll, 0.0f, 0.1f);
}

TEST(ImuHandlerTest, DisablingMadgwickResetsYawTrustForNextCalibration) {
  // Ревью PR #283 (r3630915899): SetMadgwickEnabled(false)
  // (StabilizationManager::ApplyToFilters переключает это на ходу через
  // cfg.filter.madgwick_enabled) замораживает filter_.Update()/
  // UpdateWithMag() целиком — ни один из них не вызывается, пока Мэджвик
  // выключен, а машина всё это время могла продолжать двигаться. Флаги
  // доверия при этом не трогались. Если калибровка случается сразу после
  // повторного включения — раньше, чем фильтр успеет реально сойтись
  // заново, — она могла бы закрепить курс, посчитанный по уже неактуальному,
  // замороженному кватерниону.
  FakePlatform platform;
  ImuCalibration calib;
  MadgwickFilter filter;
  ImuHandler imu(platform, calib, filter, /*read_interval_ms=*/2);
  imu.SetEnabled(true);
  platform.SetImuData(LevelImu());
  platform.SetMagData(SomeMag());

  ImuCalibData valid_calib{};
  valid_calib.valid = true;
  valid_calib.gravity_vec[0] = 0.f;
  valid_calib.gravity_vec[1] = 0.f;
  valid_calib.gravity_vec[2] = -1.f;
  valid_calib.accel_forward_vec[0] = 1.f;
  valid_calib.accel_forward_vec[1] = 0.f;
  valid_calib.accel_forward_vec[2] = 0.f;
  calib.SetData(valid_calib);

  uint32_t now_ms = 0;
  now_ms += 2;
  imu.Update(now_ms, 2);  // первая калибровка — устанавливает vehicle frame

  for (int i = 0; i < 7000; ++i) {  // 7000 * 2 мс = 14 с сходимости
    now_ms += 2;
    imu.Update(now_ms, 2);
  }
  float pitch, roll, yaw_converged;
  filter.GetEulerDeg(pitch, roll, yaw_converged);
  ASSERT_GT(std::abs(yaw_converged), 5.0f)
      << "Тест бессмысленен, если курс не сошёлся к ненулевому значению";

  // Выключаем Мэджвик — как при live-переключении cfg.filter.madgwick_enabled.
  imu.SetMadgwickEnabled(false);
  for (int i = 0; i < 500; ++i) {
    now_ms += 2;
    imu.Update(now_ms, 2);
  }
  imu.SetMadgwickEnabled(true);

  // Повторная калибровка сразу после включения — фильтр реально сойтись
  // ещё не успел. Вызываем filter.SetVehicleFrame() НАПРЯМУЮ (минуя
  // ImuHandler::UpdateVehicleFrame() с промежуточным invalid-вызовом через
  // calib.SetData(ImuCalibData{})): тот промежуточный вызов сам по себе
  // снимает use_vehicle_frame_ и маскирует проверку — здесь важно
  // изолированно проверить именно сброс yaw_has_absolute_ref_ при
  // выключении/включении Мэджвика, а не гейт «была ли предыдущая vehicle
  // frame» (см. review r3630012102, уже проверен отдельным тестом).
  filter.SetVehicleFrame(valid_calib.gravity_vec, valid_calib.accel_forward_vec,
                         true);

  float pitch_after, roll_after, yaw_after;
  filter.GetEulerDeg(pitch_after, roll_after, yaw_after);
  EXPECT_NEAR(yaw_after, 0.0f, 0.1f)
      << "После выключения/включения Мэджвика опора должна быть сброшена — "
         "калибровка обнуляет курс, а не сохраняет неактуальный";
  EXPECT_NEAR(pitch_after, 0.0f, 0.1f);
  EXPECT_NEAR(roll_after, 0.0f, 0.1f);
}

}  // namespace
}  // namespace rc_vehicle
