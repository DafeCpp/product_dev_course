#pragma once

/**
 * Фильтр Madgwick AHRS (IMU, 6DOF) для оценки ориентации по акселерометру и гироскопу.
 * Вход: ax, ay, az (g), gx, gy, gz (град/с). Выход: кватернион и углы Эйлера (pitch, roll, yaw).
 * Без магнитометра — рыскание (yaw) будет дрейфовать; pitch/roll стабильны за счёт акселерометра.
 * Платформонезависимый код (только float-математика).
 *
 * Система координат (по умолчанию — мировая NED):
 * - Кватернион q задаёт поворот из опорной СК в СК датчика (IMU): v_sensor = q ⊗ v_ref ⊗ q*.
 * - По умолчанию опорная СК = NED (X вперёд, Y вправо, Z вниз). После SetVehicleFrame() опорная СК
 *   привязана к машине: ось по вектору g (вниз), ось по направлению движения (вперёд), третья — вправо.
 *   Тогда при горизонтальной машине, смотрящей «вперёд», q = (1,0,0,0), pitch=roll=yaw=0.
 * - Углы Эйлера: ZYX. Roll — вокруг X тела, pitch — вокруг Y, yaw — вокруг Z.
 */

struct ImuData;

class MadgwickFilter {
 public:
  MadgwickFilter();

  /**
   * Обновить оценку ориентации одним семплом IMU.
   * @param ax, ay, az — ускорение в g (после калибровки)
   * @param gx, gy, gz — угловая скорость в град/с (после калибровки)
   * @param dt_sec — интервал между семплами, с (например 0.002 при 500 Гц)
   */
  void Update(float ax, float ay, float az, float gx, float gy, float gz, float dt_sec);

  /** Обновить из структуры ImuData (удобная обёртка). dt_sec — период семпла в секундах. */
  void Update(const struct ImuData& imu, float dt_sec);

  /**
   * Задать опорную СК, связанную с машиной (g и направление движения).
   * Векторы в СК датчика в момент калибровки (gravity_vec, accel_forward_vec из ImuCalibData).
   * После вызова GetQuaternion/GetEuler возвращают ориентацию датчика относительно этой СК.
   * Если valid=false или векторы не заданы — используется NED.
   */
  void SetVehicleFrame(const float gravity_vec[3], const float forward_vec[3], bool valid = true);

  /** Кватернион (qw,qx,qy,qz): поворот из опорной СК (NED или СК машины) в СК датчика. */
  void GetQuaternion(float& qw, float& qx, float& qy, float& qz) const;

  /** Углы Эйлера в радианах: pitch (X), roll (Y), yaw (Z). */
  void GetEulerRad(float& pitch_rad, float& roll_rad, float& yaw_rad) const;

  /** Углы Эйлера в градусах. */
  void GetEulerDeg(float& pitch_deg, float& roll_deg, float& yaw_deg) const;

  /** Коэффициент коррекции по акселерометру (beta). По умолчанию 0.1; больше — быстрее реакция, больше шум. */
  void SetBeta(float beta) { beta_ = beta; }
  float GetBeta() const { return beta_; }

  /** Сбросить ориентацию в единичный кватернион (горизонтально, yaw=0). */
  void Reset();

 private:
  float q0_{1.f}, q1_{0.f}, q2_{0.f}, q3_{0.f};
  float beta_{0.1f};

  // Опорная СК машины: q_veh_to_ned (поворот из СК машины в NED), только если use_vehicle_frame_
  bool use_vehicle_frame_{false};
  float q_veh_to_ned_0_{1.f}, q_veh_to_ned_1_{0.f}, q_veh_to_ned_2_{0.f}, q_veh_to_ned_3_{0.f};

  void GetQuaternionInNed(float& qw, float& qx, float& qy, float& qz) const;
  static void QuatMul(float aw, float ax, float ay, float az,
                      float bw, float bx, float by, float bz,
                      float& ow, float& ox, float& oy, float& oz);
  static float InvSqrt(float x);
};
