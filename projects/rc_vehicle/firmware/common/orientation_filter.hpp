#pragma once

/**
 * Абстрактный базовый класс для фильтров ориентации (AHRS/IMU).
 * Позволяет переключаться между различными реализациями фильтров (Madgwick,
 * Mahony, Complementary и т.д.) на лету без изменения кода, использующего
 * фильтр.
 *
 * Система координат:
 * - Кватернион q задаёт поворот из опорной СК в СК датчика (IMU): v_sensor = q
 * ⊗ v_ref ⊗ q*.
 * - Опорная СК может быть NED (по умолчанию) или привязана к машине через
 * SetVehicleFrame().
 * - Углы Эйлера: ZYX. Roll — вокруг X тела, pitch — вокруг Y, yaw — вокруг Z.
 */

struct ImuData;

class IOrientationFilter {
 public:
  virtual ~IOrientationFilter() = default;

  /**
   * Обновить оценку ориентации одним семплом IMU.
   * @param ax, ay, az — ускорение в g (после калибровки)
   * @param gx, gy, gz — угловая скорость в град/с (после калибровки)
   * @param dt_sec — интервал между семплами, с (например 0.002 при 500 Гц)
   */
  virtual void Update(float ax, float ay, float az, float gx, float gy,
                      float gz, float dt_sec) = 0;

  /**
   * Обновить из структуры ImuData (удобная обёртка).
   * @param imu — данные IMU
   * @param dt_sec — период семпла в секундах
   */
  virtual void Update(const struct ImuData& imu, float dt_sec) = 0;

  /**
   * Задать опорную СК, связанную с машиной (g и направление движения).
   * Векторы в СК датчика в момент калибровки (gravity_vec, accel_forward_vec из
   * ImuCalibData). После вызова GetQuaternion/GetEuler возвращают ориентацию
   * датчика относительно этой СК. Если valid=false или векторы не заданы —
   * используется NED.
   * @param gravity_vec — вектор гравитации в СК датчика [3]
   * @param forward_vec — вектор направления движения в СК датчика [3]
   * @param valid — флаг валидности калибровки
   */
  virtual void SetVehicleFrame(const float gravity_vec[3],
                               const float forward_vec[3],
                               bool valid = true) = 0;

  /**
   * Получить кватернион ориентации.
   * @param qw, qx, qy, qz — компоненты кватерниона (qw,qx,qy,qz): поворот из
   * опорной СК в СК датчика
   */
  virtual void GetQuaternion(float& qw, float& qx, float& qy,
                             float& qz) const = 0;

  /**
   * Получить углы Эйлера в радианах.
   * @param pitch_rad — тангаж (вокруг оси Y), радианы
   * @param roll_rad — крен (вокруг оси X), радианы
   * @param yaw_rad — рыскание (вокруг оси Z), радианы
   */
  virtual void GetEulerRad(float& pitch_rad, float& roll_rad,
                           float& yaw_rad) const = 0;

  /**
   * Получить углы Эйлера в градусах.
   * @param pitch_deg — тангаж (вокруг оси Y), градусы
   * @param roll_deg — крен (вокруг оси X), градусы
   * @param yaw_deg — рыскание (вокруг оси Z), градусы
   */
  virtual void GetEulerDeg(float& pitch_deg, float& roll_deg,
                           float& yaw_deg) const = 0;

  /**
   * Сбросить ориентацию в единичный кватернион (горизонтально, yaw=0).
   */
  virtual void Reset() = 0;

 protected:
  IOrientationFilter() = default;
  IOrientationFilter(const IOrientationFilter&) = default;
  IOrientationFilter& operator=(const IOrientationFilter&) = default;
};