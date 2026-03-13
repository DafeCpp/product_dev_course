#pragma once

#include <cmath>
#include <cstdint>

namespace rc_vehicle {

/**
 * @brief Параметры шума процесса и измерений для VehicleEkf.
 */
struct VehicleEkfNoiseParams {
  /** Шум процесса vx [м²/с²] */
  float q_vx{0.1f};
  /** Шум процесса vy [м²/с²] */
  float q_vy{0.1f};
  /** Шум процесса r [рад²/с²] */
  float q_r{0.05f};
  /** Шум измерения gyro_z [рад²/с²] */
  float r_gz{0.01f};
  /**
   * Демпфирование боковой скорости [с⁻¹].
   * Моделирует жёсткость шин в боковом направлении: при нормальном повороте
   * vy стремится к нулю. При заносе (нет бокового усилия шин) vy растёт
   * несмотря на демпфирование — реальный занос всё равно обнаруживается.
   * По умолчанию 5.0 с⁻¹ → постоянная времени ~200 мс.
   */
  float vy_decay_hz{5.0f};
};

/**
 * @brief Extended Kalman Filter (EKF) для оценки динамического состояния
 *        RC-машины на основе IMU.
 *
 * Вектор состояния: X = [vx, vy, r]ᵀ
 *   vx — продольная скорость в СК машины [м/с]
 *   vy — боковая скорость в СК машины [м/с]  (>0 = влево)
 *   r  — угловая скорость рыскания [рад/с]   (>0 = влево, CCW сверху)
 *
 * Угол заноса (slip angle): β = atan2(vy, vx) [рад]
 *   β > 0 → занос влево (машина скользит вправо относительно движения)
 *   β < 0 → занос вправо
 *
 * Модель предсказания (bicycle model, IMU-only):
 *   vx(k+1) = vx(k) + dt * (ax_imu + r * vy)
 *   vy(k+1) = vy(k) + dt * (ay_imu - r * vx)
 *   r(k+1)  = r(k)   [random walk, корректируется по gyro_z]
 *
 *   Вывод: из уравнений движения в СК машины:
 *     ax_imu = vx_dot - r * vy  →  vx_dot = ax_imu + r * vy
 *     ay_imu = vy_dot + r * vx  →  vy_dot = ay_imu - r * vx
 *
 * Модель измерения:
 *   z = gz_imu [рад/с]  →  H = [0, 0, 1]
 *
 * Ограничения (IMU-only без датчиков скорости):
 *   - vx и vy накапливают дрейф из-за интеграции ускорений
 *   - При добавлении датчиков Холла или optical flow EKF расширяется
 *   - Yaw rate оценивается точно (прямое измерение gyro_z)
 */
class VehicleEkf {
 public:
  /** Алиас типа параметров шума. */
  using NoiseParams = VehicleEkfNoiseParams;

  /** Конструктор с параметрами шума по умолчанию. */
  explicit VehicleEkf(NoiseParams params = NoiseParams{}) noexcept;

  /**
   * @brief Сброс состояния и ковариации к начальным значениям.
   * Обнуляет вектор состояния, P → диагональная матрица с единицами.
   */
  void Reset() noexcept;

  /**
   * @brief Установить начальное состояние (для инициализации и тестирования).
   * @param vx Начальная продольная скорость [м/с]
   * @param vy Начальная боковая скорость [м/с]
   * @param r  Начальная угловая скорость рыскания [рад/с]
   */
  void SetState(float vx, float vy, float r) noexcept;

  /**
   * @brief Шаг предсказания (bicycle model).
   *
   * Интегрирует модель движения на шаг dt, используя ускорения IMU как
   * управляющий вход. Обновляет вектор состояния и матрицу ковариации.
   *
   * @param ax  Ускорение по оси X (продольное) в СК машины [м/с²], без g
   * @param ay  Ускорение по оси Y (боковое) в СК машины [м/с²], без g
   * @param dt  Шаг времени [с] > 0
   */
  void Predict(float ax, float ay, float dt) noexcept;

  /**
   * @brief Шаг обновления по измерению угловой скорости рыскания.
   *
   * Корректирует оценку состояния по измерению gyro_z.
   * H = [0, 0, 1] → измерение напрямую влияет на r и через ковариацию
   * подтягивает vx и vy.
   *
   * @param gz  Угловая скорость по Z от IMU [рад/с]
   */
  void UpdateGyroZ(float gz) noexcept;

  /**
   * @brief Zero Velocity Update (ZUPT): псевдо-измерение vx=0, vy=0.
   *
   * Вызывается когда детектировано стояночное состояние (|a|≈1g, |gyro|≈0).
   * Выполняет два скалярных обновления Калмана: z=0 для vx и z=0 для vy.
   * Плавно подтягивает скорости к нулю через Kalman gain (не жёсткий сброс).
   *
   * @param r_zupt  Шум псевдо-измерения [м²/с²]. Меньше → агрессивнее сброс.
   *                По умолчанию 0.1 — мягкий, но быстрый сброс дрейфа.
   */
  void UpdateZeroVelocity(float r_zupt = 0.1f) noexcept;

  // ─── Доступ к состоянию ───────────────────────────────────────────────

  /** Оценка продольной скорости [м/с]. */
  [[nodiscard]] float GetVx() const noexcept { return x_[0]; }

  /** Оценка боковой скорости [м/с]. */
  [[nodiscard]] float GetVy() const noexcept { return x_[1]; }

  /** Оценка угловой скорости рыскания [рад/с]. */
  [[nodiscard]] float GetYawRate() const noexcept { return x_[2]; }

  /** Модуль скорости в горизонтальной плоскости [м/с]. */
  [[nodiscard]] float GetSpeedMs() const noexcept {
    return std::sqrt(x_[0] * x_[0] + x_[1] * x_[1]);
  }

  /**
   * @brief Угол заноса в радианах: β = atan2(vy, vx).
   * Диапазон: [-π, π]. При нулевой скорости возвращает 0.
   */
  [[nodiscard]] float GetSlipAngleRad() const noexcept;

  /**
   * @brief Угол заноса в градусах.
   * Диапазон: [-180, 180]. При нулевой скорости возвращает 0.
   */
  [[nodiscard]] float GetSlipAngleDeg() const noexcept;

  // ─── Неопределённость (диагональные элементы P) ──────────────────────

  /** Дисперсия оценки vx: P[0][0]. */
  [[nodiscard]] float GetVxVariance() const noexcept { return P_[0]; }

  /** Дисперсия оценки vy: P[1][1]. */
  [[nodiscard]] float GetVyVariance() const noexcept { return P_[4]; }

  /** Дисперсия оценки r: P[2][2]. */
  [[nodiscard]] float GetRVariance() const noexcept { return P_[8]; }

  /** Установить параметры шума. */
  void SetNoiseParams(NoiseParams params) noexcept { params_ = params; }

 private:
  // Вектор состояния: [vx, vy, r]
  float x_[3]{0.0f, 0.0f, 0.0f};

  // Матрица ковариации 3×3 (row-major: P_[i*3+j] = P[i][j])
  float P_[9]{};

  // Параметры шума
  NoiseParams params_;

  // Вспомогательные методы
  void InitP() noexcept;

  /**
   * Проверяет P на NaN/Inf (→ сброс к InitP) и ограничивает диагональные
   * элементы отрезком [kPDiagMin, kPDiagMax], предотвращая расходимость
   * ковариации при отсутствии измерений скорости.
   */
  void ClampP() noexcept;

  static void MatMul3x3(const float A[9], const float B[9],
                        float C[9]) noexcept;
  static void MatTranspose3x3(const float A[9], float At[9]) noexcept;
  static void SymmetrizeP(float P[9]) noexcept;

  // Границы диагональных элементов P: RC-машина, скорость ≤ 10 м/с, r ≤ 10 рад/с
  static constexpr float kPDiagMax = 1e3f;   // Верхний предел дисперсии
  static constexpr float kPDiagMin = 1e-6f;  // Нижний предел (защита от вырождения)
};

}  // namespace rc_vehicle
