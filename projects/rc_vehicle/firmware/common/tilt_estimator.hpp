#pragma once

#include <cstdint>

struct ImuData;

namespace rc_vehicle {

/**
 * @brief Параметры комплементарного тилт-фильтра TiltEstimator.
 * Вынесены на уровень namespace (а не вложены в класс), т.к. GCC не
 * позволяет использовать вложенный тип с default member initializers как
 * тип-по-умолчанию параметра конструктора того же класса (см. аналогичный
 * паттерн VehicleEkfNoiseParams в vehicle_ekf.hpp).
 */
struct TiltEstimatorParams {
  /** Коэффициент комплементарного подтяга к accel-оценке [1/с]. Малый →
   * меньше остаточной утечки ложного тилта на разгоне, но медленнее
   * компенсация дрейфа гиро. По умолчанию 0.5 (постоянная времени ~2с). */
  float corr_gain_hz{0.5f};
  /** Полуширина гейта |a|-1g для accel-коррекции [g]. Вне диапазона
   * (удар/выброс) коррекция не применяется — тангаж держится на гиро.
   * По умолчанию 0.1. */
  float accel_gate_band_g{0.1f};
};

/**
 * @brief Комплементарный тилт-фильтр (pitch/roll), не загрязняемый линейным
 *        ускорением (LOS-240).
 *
 * Проблема источника-предшественника (Madgwick, LOS-232/PR#287): акселерометр
 * меряет удельную силу = гравитация − линейное ускорение и не различает их.
 * При плавном продольном разгоне (0.2g) |a|≈1.02g остаётся ниже порога
 * adaptive-beta (0.2g) → Madgwick принимает удельную силу за наклон и
 * заваливает pitch, а grav-comp затем вычитает уже РЕАЛЬНОЕ ускорение.
 *
 * TiltEstimator решает это иначе:
 *  1) Гироскоп меряет угловую СКОРОСТЬ и не видит линейного ускорения —
 *     гиро-пропагация (pitch += gy·dt, roll += gx·dt) держит тангаж верным
 *     на разгоне (корпус не наклоняется).
 *  2) Акселерометр используется только как МЕДЛЕННЫЙ якорь против дрейфа
 *     гиро, причём перед оценкой гравитационного угла из него вычитается
 *     известное продольное линейное ускорение (a_lin_long_g, из заякоренного
 *     EKF vx) и известное боковое/центростремительное ускорение
 *     (a_lin_lat_g, из vx·yaw_rate — иначе разворот с боковым ускорением
 *     ~0.2g симметрично заваливал бы roll тем же путём, что продольный
 *     разгон заваливал pitch без a_lin_long_g) — остаток ≈ чистая гравитация.
 *  3) Коррекция гейтится по модулю скорректированного вектора: если он не
 *     близок к 1g (удар/выброс на ухабе), акселерометру не доверяют вовсе, и
 *     тангаж временно держится только на гироскопе.
 *
 * Конвенция углов ZYX (согласована с VehicleEkf::UpdateFromImu): pitch —
 * вокруг оси Y тела (из gy), roll — вокруг оси X тела (из gx).
 * grav_x = -sin(pitch), grav_y = cos(pitch)·sin(roll).
 *
 * Малоугловое приближение gyro→pitch/roll (без полного интегрирования
 * кватерниона) валидно для наземной машины: rate вокруг Z (рыскание) не
 * вносит вклад в pitch/roll при малых pitch/roll, что выполняется в
 * штатной езде RC-машины.
 */
class TiltEstimator {
 public:
  /** Алиас типа параметров. */
  using Params = TiltEstimatorParams;

  /** Конструктор с параметрами по умолчанию. */
  explicit TiltEstimator(Params params = Params{}) noexcept : params_(params) {}

  /** Сброс к pitch=roll=0. */
  void Reset() noexcept;

  /**
   * @brief Обновить оценку тангажа/крена на один тик control loop.
   * @param imu           Сырые данные IMU (ax,ay,az в g; gx,gy,gz в °/с).
   * @param a_lin_long_g  Известное продольное линейное ускорение в СК
   *                      кузова [g] (напр. из конечной разности заякоренного
   *                      EKF vx), вычитается из ax перед accel-коррекцией.
   * @param a_lin_lat_g   Известное боковое (центростремительное) ускорение
   *                      в СК кузова [g] (напр. vx·yaw_rate — LOS-240,
   *                      код-ревью PR #290, 6-й раунд), вычитается из ay.
   *                      Без этого разворот с боковым ускорением ~0.2g
   *                      симметрично заваливает roll тем же путём, что и
   *                      продольный разгон заваливал pitch (см. класс).
   * @param dt_sec        Шаг времени [с] > 0.
   */
  void Update(const ImuData& imu, float a_lin_long_g, float a_lin_lat_g,
              float dt_sec) noexcept;

  /** Оценка тангажа [рад]. */
  [[nodiscard]] float GetPitchRad() const noexcept { return pitch_rad_; }

  /** Оценка крена [рад]. */
  [[nodiscard]] float GetRollRad() const noexcept { return roll_rad_; }

  /** Установить параметры. */
  void SetParams(Params params) noexcept { params_ = params; }

 private:
  float pitch_rad_{0.0f};
  float roll_rad_{0.0f};
  Params params_;

  /** Физический предел |pitch|,|roll| для клемпа против разноса гиро [рад]
   * (~60°, с запасом выше любого реалистичного наклона RC-машины). */
  static constexpr float kMaxTiltRad = 1.0472f;  // 60°

  static float WrapAngle(float a) noexcept;
  static float ClampTilt(float a) noexcept;
};

}  // namespace rc_vehicle
