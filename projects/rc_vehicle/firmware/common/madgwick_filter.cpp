#include "madgwick_filter.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "mpu6050_spi.hpp"  // ImuData definition

namespace {

constexpr float kDegToRad = 0.01745329252f;  // π/180

}  // namespace

namespace rc_vehicle {

MadgwickFilter::MadgwickFilter() { Reset(); }

void MadgwickFilter::Reset() {
  q0_ = 1.f;
  q1_ = 0.f;
  q2_ = 0.f;
  q3_ = 0.f;
}

void MadgwickFilter::Update(float ax, float ay, float az, float gx, float gy,
                            float gz, float dt_sec) {
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
    // Адаптивный beta: при линейном ускорении |a| отклоняется от 1g.
    // Акселерометр перестаёт быть надёжным ориентиром гравитации → отключаем
    // коррекцию (beta=0), чтобы не вносить ошибку в ориентацию при разгоне,
    // торможении и поворотах.
    float effective_beta = beta_;
    if (adaptive_enabled_) {
      const float accel_mag = std::sqrt(norm2);
      if (std::fabs(accel_mag - 1.0f) > adaptive_threshold_g_) {
        effective_beta = 0.0f;
      }
    }

    const float recipNorm = InvSqrt(norm2);
    ax_n *= recipNorm;
    ay_n *= recipNorm;
    az_n *= recipNorm;

    const float _2q0 = 2.f * q0_, _2q1 = 2.f * q1_, _2q2 = 2.f * q2_,
                _2q3 = 2.f * q3_;
    const float _4q0 = 4.f * q0_, _4q1 = 4.f * q1_, _4q2 = 4.f * q2_;
    const float _8q1 = 8.f * q1_, _8q2 = 8.f * q2_;
    const float q0q0 = q0_ * q0_, q1q1 = q1_ * q1_, q2q2 = q2_ * q2_,
                q3q3 = q3_ * q3_;

    // Градиент (шаг коррекции)
    float s0 = _4q0 * q2q2 + _2q2 * ax_n + _4q0 * q1q1 - _2q1 * ay_n;
    float s1 = _4q1 * q3q3 - _2q3 * ax_n + 4.f * q0q0 * q1_ - _2q0 * ay_n -
               _4q1 + _8q1 * q1q1 + _8q1 * q2q2 + _4q1 * az_n;
    float s2 = 4.f * q0q0 * q2_ + _2q0 * ax_n + _4q2 * q3q3 - _2q3 * ay_n -
               _4q2 + _8q2 * q1q1 + _8q2 * q2q2 + _4q2 * az_n;
    float s3 = 4.f * q1q1 * q3_ - _2q1 * ax_n + 4.f * q2q2 * q3_ - _2q2 * ay_n;

    const float sSqNorm = s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3;
    // Пропускаем коррекцию если градиент вырожден (кватернион точно выровнен с g)
    // или subnormal float — умножение на InvSqrt(~0) даст NaN/Inf в кватернионе
    if (sSqNorm >= 1e-20f) {
      const float sNorm = InvSqrt(sSqNorm);
      s0 *= sNorm;
      s1 *= sNorm;
      s2 *= sNorm;
      s3 *= sNorm;

      qDot1 -= effective_beta * s0;
      qDot2 -= effective_beta * s1;
      qDot3 -= effective_beta * s2;
      qDot4 -= effective_beta * s3;
    }
  }

  // Интегрирование
  q0_ += qDot1 * dt_sec;
  q1_ += qDot2 * dt_sec;
  q2_ += qDot3 * dt_sec;
  q3_ += qDot4 * dt_sec;

  // Нормализация кватерниона
  const float qSqNorm = q0_ * q0_ + q1_ * q1_ + q2_ * q2_ + q3_ * q3_;
  if (qSqNorm < 1e-12f) {
    // Singularity: норма кватерниона близка к нулю — сбрасываем на единичный
    q0_ = 1.f;
    q1_ = 0.f;
    q2_ = 0.f;
    q3_ = 0.f;
    return;
  }
  const float qNorm = InvSqrt(qSqNorm);
  q0_ *= qNorm;
  q1_ *= qNorm;
  q2_ *= qNorm;
  q3_ *= qNorm;
}

void MadgwickFilter::Update(const ImuData& imu, float dt_sec) {
  Update(imu.ax, imu.ay, imu.az, imu.gx, imu.gy, imu.gz, dt_sec);
}

void MadgwickFilter::UpdateWithMag(float ax, float ay, float az, float gx,
                                   float gy, float gz, float mx, float my,
                                   float mz, float dt_sec) {
  if (dt_sec <= 0.f) return;

  const float gx_rad = gx * kDegToRad;
  const float gy_rad = gy * kDegToRad;
  const float gz_rad = gz * kDegToRad;

  // Производная кватерниона от гироскопа
  float qDot1 = 0.5f * (-q1_ * gx_rad - q2_ * gy_rad - q3_ * gz_rad);
  float qDot2 = 0.5f * (q0_ * gx_rad + q2_ * gz_rad - q3_ * gy_rad);
  float qDot3 = 0.5f * (q0_ * gy_rad - q1_ * gz_rad + q3_ * gx_rad);
  float qDot4 = 0.5f * (q0_ * gz_rad + q1_ * gy_rad - q2_ * gx_rad);

  const float anorm2 = ax * ax + ay * ay + az * az;
  const float mnorm2 = mx * mx + my * my + mz * mz;

  if (anorm2 > 1e-12f && mnorm2 > 1e-12f) {
    // Адаптивный beta по акселерометру
    float effective_beta = beta_;
    if (adaptive_enabled_) {
      const float accel_mag = std::sqrt(anorm2);
      if (std::fabs(accel_mag - 1.0f) > adaptive_threshold_g_) {
        effective_beta = 0.0f;
      }
    }

    const float an = InvSqrt(anorm2);
    const float ax_n = ax * an, ay_n = ay * an, az_n = az * an;
    const float mn = InvSqrt(mnorm2);
    const float mx_n = mx * mn, my_n = my * mn, mz_n = mz * mn;

    // Вспомогательные произведения компонент кватерниона
    const float q0q0 = q0_ * q0_, q0q1 = q0_ * q1_, q0q2 = q0_ * q2_,
                q0q3 = q0_ * q3_;
    const float q1q1 = q1_ * q1_, q1q2 = q1_ * q2_, q1q3 = q1_ * q3_;
    const float q2q2 = q2_ * q2_, q2q3 = q2_ * q3_;
    const float q3q3 = q3_ * q3_;

    // Вычисляем опорное магнитное поле Земли из текущего кватерниона:
    // h = q ⊗ [0, mx, my, mz] ⊗ q* — поле в earth-frame
    const float hx = mx_n * (q0q0 + q1q1 - q2q2 - q3q3) +
                     2.f * my_n * (q1q2 - q0q3) +
                     2.f * mz_n * (q1q3 + q0q2);
    const float hy = 2.f * mx_n * (q1q2 + q0q3) +
                     my_n * (q0q0 - q1q1 + q2q2 - q3q3) +
                     2.f * mz_n * (q2q3 - q0q1);
    const float hz = 2.f * mx_n * (q1q3 - q0q2) +
                     2.f * my_n * (q2q3 + q0q1) +
                     mz_n * (q0q0 - q1q1 - q2q2 + q3q3);

    // Проецируем в горизонтальную плоскость: bx = sqrt(hx²+hy²), bz = hz.
    // Это устраняет зависимость от магнитного склонения.
    const float bx = std::sqrt(hx * hx + hy * hy);
    const float bz = hz;
    const float _2bx = 2.f * bx, _2bz = 2.f * bz;
    const float _4bx = 2.f * _2bx, _4bz = 2.f * _2bz;

    // Целевые функции: f_g (акселерометр) + f_b (магнетометр)
    // f_g: оценка разницы между предсказанным и измеренным вектором g
    const float fg1 = 2.f * (q1q3 - q0q2) - ax_n;
    const float fg2 = 2.f * (q0q1 + q2q3) - ay_n;
    const float fg3 = 2.f * (0.5f - q1q1 - q2q2) - az_n;
    // f_b: оценка разницы предсказанного и измеренного поля в earth-frame
    const float fb1 = _2bx * (0.5f - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx_n;
    const float fb2 = _2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my_n;
    const float fb3 = _2bx * (q0q2 + q1q3) + _2bz * (0.5f - q1q1 - q2q2) - mz_n;

    // Градиент: J^T * f (объединённый для g и b)
    float s0 = -2.f * q2_ * fg1 + 2.f * q1_ * fg2 +
               (-_2bz * q2_) * fb1 + (-_2bx * q3_ + _2bz * q1_) * fb2 +
               _2bx * q2_ * fb3;
    float s1 = 2.f * q3_ * fg1 + 2.f * q0_ * fg2 - 4.f * q1_ * fg3 +
               _2bz * q3_ * fb1 + (_2bx * q2_ + _2bz * q0_) * fb2 +
               (_2bx * q3_ - _4bz * q1_) * fb3;
    float s2 = -2.f * q0_ * fg1 + 2.f * q3_ * fg2 - 4.f * q2_ * fg3 +
               (-_4bx * q2_ - _2bz * q0_) * fb1 +
               (_2bx * q1_ + _2bz * q3_) * fb2 +
               (_2bx * q0_ - _4bz * q2_) * fb3;
    float s3 = 2.f * q1_ * fg1 + 2.f * q2_ * fg2 +
               (-_4bx * q3_ + _2bz * q1_) * fb1 +
               (-_2bx * q0_ + _2bz * q2_) * fb2 + _2bx * q1_ * fb3;

    const float sSqNorm = s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3;
    if (sSqNorm >= 1e-20f) {
      const float sNorm = InvSqrt(sSqNorm);
      s0 *= sNorm; s1 *= sNorm; s2 *= sNorm; s3 *= sNorm;
      qDot1 -= effective_beta * s0;
      qDot2 -= effective_beta * s1;
      qDot3 -= effective_beta * s2;
      qDot4 -= effective_beta * s3;
    }
  } else if (anorm2 > 1e-12f) {
    // Нет mag — деградируем до 6DOF
    Update(ax, ay, az, gx, gy, gz, dt_sec);
    return;
  }

  // Интегрирование и нормализация
  q0_ += qDot1 * dt_sec;
  q1_ += qDot2 * dt_sec;
  q2_ += qDot3 * dt_sec;
  q3_ += qDot4 * dt_sec;

  const float qSqNorm = q0_ * q0_ + q1_ * q1_ + q2_ * q2_ + q3_ * q3_;
  if (qSqNorm < 1e-12f) {
    q0_ = 1.f; q1_ = 0.f; q2_ = 0.f; q3_ = 0.f;
    return;
  }
  const float qNorm = InvSqrt(qSqNorm);
  q0_ *= qNorm; q1_ *= qNorm; q2_ *= qNorm; q3_ *= qNorm;
}

void MadgwickFilter::SetVehicleFrame(const float gravity_vec[3],
                                     const float forward_vec[3], bool valid) {
  use_vehicle_frame_ = false;
  if (!valid || forward_vec == nullptr || gravity_vec == nullptr) return;

  // Z_veh = gravity_vec в СК датчика (показание акселерометра в покое),
  // нормализованный. Направлен ВВЕРХ (реакция опоры, противоположно g).
  // При нормальном монтаже ≈ [0,0,+1], при перевёрнутом ≈ [0,0,-1].
  float zx = gravity_vec[0], zy = gravity_vec[1], zz = gravity_vec[2];
  float z2 = zx * zx + zy * zy + zz * zz;
  if (z2 < 1e-12f) return;
  float zn = InvSqrt(z2);
  zx *= zn;
  zy *= zn;
  zz *= zn;

  // X_veh (вперёд машины) = forward_vec, ортогонализированный к Z_veh.
  // Проекция: f_orth = forward - (forward · Z_veh) * Z_veh
  float fx = forward_vec[0], fy = forward_vec[1], fz = forward_vec[2];
  float dot_fz = fx * zx + fy * zy + fz * zz;
  fx -= dot_fz * zx;
  fy -= dot_fz * zy;
  fz -= dot_fz * zz;
  float f2 = fx * fx + fy * fy + fz * fz;
  if (f2 < 1e-12f) return;  // forward ∥ gravity — невозможно построить СК
  float fn = InvSqrt(f2);
  fx *= fn;
  fy *= fn;
  fz *= fn;

  // Y_veh (вправо) = Z_veh × X_veh
  float yx = zy * fz - zz * fy;
  float yy = zz * fx - zx * fz;
  float yz = zx * fy - zy * fx;

  // R_veh_to_ned: столбцы = оси СК машины в СК датчика (X_veh, Y_veh, Z_veh)
  float r00 = fx, r10 = fy, r20 = fz;
  float r01 = yx, r11 = yy, r21 = yz;
  float r02 = zx, r12 = zy, r22 = zz;

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
  float qn = InvSqrt(
      q_veh_to_ned_0_ * q_veh_to_ned_0_ + q_veh_to_ned_1_ * q_veh_to_ned_1_ +
      q_veh_to_ned_2_ * q_veh_to_ned_2_ + q_veh_to_ned_3_ * q_veh_to_ned_3_);
  q_veh_to_ned_0_ *= qn;
  q_veh_to_ned_1_ *= qn;
  q_veh_to_ned_2_ *= qn;
  q_veh_to_ned_3_ *= qn;
  use_vehicle_frame_ = true;

  // Инициализировать кватернион Мэджвика так, чтобы vehicle-frame Euler = 0.
  // Мэджвик использует сопряжённую конвенцию: v_sensor = q* ⊗ v_ref ⊗ q,
  // т.е. q в стандартной конвенции = sensor→reference.
  // GetQuaternion: q_result = q_madgwick * q_sv (vehicle→reference в стандартной).
  // Для identity: q_madgwick * q_sv = I  ⟹  q_madgwick = conj(q_sv).
  q0_ = q_veh_to_ned_0_;
  q1_ = -q_veh_to_ned_1_;
  q2_ = -q_veh_to_ned_2_;
  q3_ = -q_veh_to_ned_3_;
}

void MadgwickFilter::GetQuaternionInNed(float& qw, float& qx, float& qy,
                                        float& qz) const {
  qw = q0_;
  qx = q1_;
  qy = q2_;
  qz = q3_;
}

void MadgwickFilter::GetQuaternion(float& qw, float& qx, float& qy,
                                   float& qz) const {
  if (!use_vehicle_frame_) {
    GetQuaternionInNed(qw, qx, qy, qz);
    return;
  }
  // Мэджвик (сопряжённая конвенция): q_madgwick = sensor→reference (стандартная).
  // q_sv = vehicle→sensor (стандартная).
  // q_result = q_madgwick * q_sv = vehicle→reference (стандартная конвенция).
  // Euler ZYX из q_result дают ориентацию машины относительно горизонта.
  QuatMul(q0_, q1_, q2_, q3_, q_veh_to_ned_0_, q_veh_to_ned_1_, q_veh_to_ned_2_,
          q_veh_to_ned_3_, qw, qx, qy, qz);
}

void MadgwickFilter::QuatMul(float aw, float ax, float ay, float az, float bw,
                             float bx, float by, float bz, float& ow, float& ox,
                             float& oy, float& oz) {
  ow = aw * bw - ax * bx - ay * by - az * bz;
  ox = aw * bx + ax * bw + ay * bz - az * by;
  oy = aw * by - ax * bz + ay * bw + az * bx;
  oz = aw * bz + ax * by - ay * bx + az * bw;
}

void MadgwickFilter::GetEulerRad(float& pitch_rad, float& roll_rad,
                                 float& yaw_rad) const {
  float qw, qx, qy, qz;
  GetQuaternion(qw, qx, qy, qz);
  roll_rad =
      std::atan2(2.f * (qw * qx + qy * qz), 1.f - 2.f * (qx * qx + qy * qy));
  pitch_rad = std::asin(std::clamp(2.f * (qw * qy - qz * qx), -1.f, 1.f));
  yaw_rad =
      std::atan2(2.f * (qw * qz + qx * qy), 1.f - 2.f * (qy * qy + qz * qz));
}

void MadgwickFilter::GetEulerDeg(float& pitch_deg, float& roll_deg,
                                 float& yaw_deg) const {
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

}  // namespace rc_vehicle
