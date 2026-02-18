#include "madgwick_filter.hpp"

#include <cmath>
#include <cstring>

#include "mpu6050_spi.hpp"

namespace {

constexpr float kDegToRad = 0.01745329252f;  // π/180

}  // namespace

MadgwickFilter::MadgwickFilter() {
  Reset();
}

void MadgwickFilter::Reset() {
  q0_ = 1.f;
  q1_ = 0.f;
  q2_ = 0.f;
  q3_ = 0.f;
}

void MadgwickFilter::Update(float ax, float ay, float az, float gx, float gy, float gz, float dt_sec) {
  if (dt_sec <= 0.f) return;

  // Гироскоп: град/с → рад/с
  const float gx_rad = gx * kDegToRad;
  const float gy_rad = gy * kDegToRad;
  const float gz_rad = gz * kDegToRad;

  // Производная кватерниона от гироскопа: q_dot = 0.5 * q ⊗ [0, ω]
  float qDot1 = 0.5f * (-q1_ * gx_rad - q2_ * gy_rad - q3_ * gz_rad);
  float qDot2 = 0.5f * (q0_ * gx_rad + q2_ * gz_rad - q3_ * gy_rad);
  float qDot3 = 0.5f * (q0_ * gy_rad - q1_ * gz_rad + q3_ * gx_rad);
  float qDot4 = 0.5f * (q0_ * gz_rad + q1_ * gy_rad - q2_ * gx_rad);

  // Коррекция по акселерометру (градиентный спуск), если измерение валидно
  float ax_n = ax, ay_n = ay, az_n = az;
  const float norm2 = ax * ax + ay * ay + az * az;
  if (norm2 > 1e-12f) {
    const float recipNorm = InvSqrt(norm2);
    ax_n *= recipNorm;
    ay_n *= recipNorm;
    az_n *= recipNorm;

    const float _2q0 = 2.f * q0_, _2q1 = 2.f * q1_, _2q2 = 2.f * q2_, _2q3 = 2.f * q3_;
    const float _4q0 = 4.f * q0_, _4q1 = 4.f * q1_, _4q2 = 4.f * q2_;
    const float _8q1 = 8.f * q1_, _8q2 = 8.f * q2_;
    const float q0q0 = q0_ * q0_, q1q1 = q1_ * q1_, q2q2 = q2_ * q2_, q3q3 = q3_ * q3_;

    // Градиент (шаг коррекции)
    float s0 = _4q0 * q2q2 + _2q2 * ax_n + _4q0 * q1q1 - _2q1 * ay_n;
    float s1 = _4q1 * q3q3 - _2q3 * ax_n + 4.f * q0q0 * q1_ - _2q0 * ay_n - _4q1 + _8q1 * q1q1 + _8q1 * q2q2 + _4q1 * az_n;
    float s2 = 4.f * q0q0 * q2_ + _2q0 * ax_n + _4q2 * q3q3 - _2q3 * ay_n - _4q2 + _8q2 * q1q1 + _8q2 * q2q2 + _4q2 * az_n;
    float s3 = 4.f * q1q1 * q3_ - _2q1 * ax_n + 4.f * q2q2 * q3_ - _2q2 * ay_n;

    const float sNorm = InvSqrt(s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3);
    s0 *= sNorm;
    s1 *= sNorm;
    s2 *= sNorm;
    s3 *= sNorm;

    qDot1 -= beta_ * s0;
    qDot2 -= beta_ * s1;
    qDot3 -= beta_ * s2;
    qDot4 -= beta_ * s3;
  }

  // Интегрирование
  q0_ += qDot1 * dt_sec;
  q1_ += qDot2 * dt_sec;
  q2_ += qDot3 * dt_sec;
  q3_ += qDot4 * dt_sec;

  // Нормализация кватерниона
  const float qNorm = InvSqrt(q0_ * q0_ + q1_ * q1_ + q2_ * q2_ + q3_ * q3_);
  q0_ *= qNorm;
  q1_ *= qNorm;
  q2_ *= qNorm;
  q3_ *= qNorm;
}

void MadgwickFilter::Update(const ImuData& imu, float dt_sec) {
  Update(imu.ax, imu.ay, imu.az, imu.gx, imu.gy, imu.gz, dt_sec);
}

void MadgwickFilter::SetVehicleFrame(const float gravity_vec[3], const float forward_vec[3], bool valid) {
  use_vehicle_frame_ = false;
  if (!valid || forward_vec == nullptr) return;
  (void)gravity_vec;

  // СК машины в NED: Z_veh = вниз (0,0,1), X_veh = вперёд (проекция forward на горизонталь), Y_veh = Z × X
  float zx = 0.f, zy = 0.f, zz = 1.f;
  float fx = forward_vec[0], fy = forward_vec[1], fz = 0.f;  // проекция на горизонталь (Z=0)
  float nx2 = fx * fx + fy * fy;
  if (nx2 < 1e-12f) return;
  float nx = InvSqrt(nx2);
  fx *= nx;
  fy *= nx;

  float yx = zy * fz - zz * fy;  // (0,0,1)×(fx,fy,0) = (-fy, fx, 0)
  float yy = zz * fx - zx * fz;
  float yz = zx * fy - zy * fx;

  // R_veh_to_ned: столбцы = оси СК машины в NED (X_veh, Y_veh, Z_veh)
  float r00 = fx, r10 = fy, r20 = 0.f;
  float r01 = yx, r11 = yy, r21 = 0.f;
  float r02 = 0.f, r12 = 0.f, r22 = 1.f;

  // Матрица → кватернион q_veh_to_ned
  float tr = r00 + r11 + r22;
  if (tr > 0.f) {
    float s = 0.5f / std::sqrt(tr + 1.f);
    q_veh_to_ned_0_ = 0.25f / s;
    q_veh_to_ned_1_ = (r21 - r12) * s;
    q_veh_to_ned_2_ = (r02 - r20) * s;
    q_veh_to_ned_3_ = (r10 - r01) * s;
  } else {
    if (r00 >= r11 && r00 >= r22) {
      float s = 2.f * std::sqrt(1.f + r00 - r11 - r22);
      q_veh_to_ned_0_ = (r21 - r12) / s;
      q_veh_to_ned_1_ = 0.25f * s;
      q_veh_to_ned_2_ = (r01 + r10) / s;
      q_veh_to_ned_3_ = (r02 + r20) / s;
    } else if (r11 >= r22) {
      float s = 2.f * std::sqrt(1.f + r11 - r00 - r22);
      q_veh_to_ned_0_ = (r02 - r20) / s;
      q_veh_to_ned_1_ = (r01 + r10) / s;
      q_veh_to_ned_2_ = 0.25f * s;
      q_veh_to_ned_3_ = (r12 + r21) / s;
    } else {
      float s = 2.f * std::sqrt(1.f + r22 - r00 - r11);
      q_veh_to_ned_0_ = (r10 - r01) / s;
      q_veh_to_ned_1_ = (r02 + r20) / s;
      q_veh_to_ned_2_ = (r12 + r21) / s;
      q_veh_to_ned_3_ = 0.25f * s;
    }
  }
  float qn = InvSqrt(q_veh_to_ned_0_ * q_veh_to_ned_0_ + q_veh_to_ned_1_ * q_veh_to_ned_1_ +
                     q_veh_to_ned_2_ * q_veh_to_ned_2_ + q_veh_to_ned_3_ * q_veh_to_ned_3_);
  q_veh_to_ned_0_ *= qn;
  q_veh_to_ned_1_ *= qn;
  q_veh_to_ned_2_ *= qn;
  q_veh_to_ned_3_ *= qn;
  use_vehicle_frame_ = true;
}

void MadgwickFilter::GetQuaternionInNed(float& qw, float& qx, float& qy, float& qz) const {
  qw = q0_;
  qx = q1_;
  qy = q2_;
  qz = q3_;
}

void MadgwickFilter::GetQuaternion(float& qw, float& qx, float& qy, float& qz) const {
  if (!use_vehicle_frame_) {
    GetQuaternionInNed(qw, qx, qy, qz);
    return;
  }
  // q_sensor_from_veh = q_sensor_from_ned * q_veh_to_ned
  QuatMul(q0_, q1_, q2_, q3_, q_veh_to_ned_0_, q_veh_to_ned_1_, q_veh_to_ned_2_, q_veh_to_ned_3_, qw, qx, qy, qz);
}

void MadgwickFilter::QuatMul(float aw, float ax, float ay, float az,
                             float bw, float bx, float by, float bz,
                             float& ow, float& ox, float& oy, float& oz) {
  ow = aw * bw - ax * bx - ay * by - az * bz;
  ox = aw * bx + ax * bw + ay * bz - az * by;
  oy = aw * by - ax * bz + ay * bw + az * bx;
  oz = aw * bz + ax * by - ay * bx + az * bw;
}

void MadgwickFilter::GetEulerRad(float& pitch_rad, float& roll_rad, float& yaw_rad) const {
  float qw, qx, qy, qz;
  GetQuaternion(qw, qx, qy, qz);
  roll_rad = std::atan2(2.f * (qw * qx + qy * qz), 1.f - 2.f * (qx * qx + qy * qy));
  pitch_rad = std::asin(2.f * (qw * qy - qz * qx));
  yaw_rad = std::atan2(2.f * (qw * qz + qx * qy), 1.f - 2.f * (qy * qy + qz * qz));
}

void MadgwickFilter::GetEulerDeg(float& pitch_deg, float& roll_deg, float& yaw_deg) const {
  float pr, rr, yr;
  GetEulerRad(pr, rr, yr);
  constexpr float kRadToDeg = 57.295779513f;  // 180/π
  pitch_deg = pr * kRadToDeg;
  roll_deg = rr * kRadToDeg;
  yaw_deg = yr * kRadToDeg;
}

float MadgwickFilter::InvSqrt(float x) {
  if (x <= 0.f) return 0.f;
  return 1.f / std::sqrt(x);
}
