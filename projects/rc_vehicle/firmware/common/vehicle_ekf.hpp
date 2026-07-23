#pragma once

#include <cmath>
#include <cstdint>

namespace rc_vehicle {

/** Последний результат проверки условий Zero Velocity Update. */
enum class ZuptStatus : uint8_t {
  NotEvaluated = 0,
  Applied,
  ThrottleRejected,
  AccelRejected,
  GyroRejected,
};

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
  /**
   * Шум процесса курсового угла ψ [рад²].
   * Малый — ψ хорошо интегрируется из r; ненулевой для стабильности.
   */
  float q_psi{0.001f};
  /**
   * Шум измерения курса от магнитометра [рад²].
   * 0.01 ≈ (5.7°)² — типовая точность tilt-compensated heading.
   */
  float r_heading{0.01f};
};

/**
 * @brief Extended Kalman Filter (EKF) для оценки динамического состояния
 *        RC-машины на основе IMU + магнитометра.
 *
 * Вектор состояния: X = [vx, vy, r, ψ]ᵀ
 *   vx — продольная скорость в СК машины [м/с]
 *   vy — боковая скорость в СК машины [м/с]  (>0 = влево)
 *   r  — угловая скорость рыскания [рад/с]   (>0 = влево, CCW сверху)
 *   ψ  — курсовой угол в мировой СК [рад]    (диапазон [-π, π])
 *
 * Угол заноса (slip angle): β = atan2(vy, vx) [рад]
 *
 * Модель предсказания (bicycle model + интеграция курса):
 *   vx(k+1) = vx(k) + dt * (ax_imu + r * vy)
 *   vy(k+1) = vy(k) + dt * (ay_imu - r * vx)   [+ vy damping]
 *   r(k+1)  = r(k)   [random walk, корректируется по gyro_z]
 *   ψ(k+1)  = ψ(k) + dt * r
 *
 * Якобиан F (4×4):
 *   F = [ 1,       dt·r,   dt·vy,  0 ]
 *       [ -dt·r,   vy_dmp, -dt·vx, 0 ]
 *       [ 0,       0,      1,      0 ]
 *       [ 0,       0,      dt,     1 ]
 *
 * Модели измерений:
 *   UpdateGyroZ:   H = [0, 0, 1, 0]  →  z = gz [рад/с]
 *   UpdateHeading: H = [0, 0, 0, 1]  →  z = ψ_mag [рад], с обёрткой угла
 *
 * Ограничения (IMU-only без датчиков скорости):
 *   - vx и vy накапливают дрейф из-за интеграции ускорений
 *   - При добавлении датчиков Холла или optical flow EKF расширяется
 *   - Yaw rate оценивается точно (прямое измерение gyro_z)
 *   - Курс ψ корректируется магнитометром через UpdateHeading()
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
   * @brief Установить начальный курсовой угол.
   * @param yaw_rad Курсовой угол в радианах [-π, π]
   */
  void SetYaw(float yaw_rad) noexcept;

  /**
   * @brief Шаг предсказания (bicycle model + интеграция ψ).
   *
   * @param ax  Ускорение по оси X (продольное) в СК машины [м/с²], без g
   * @param ay  Ускорение по оси Y (боковое) в СК машины [м/с²], без g
   * @param dt  Шаг времени [с] > 0
   */
  void Predict(float ax, float ay, float dt) noexcept;

  /**
   * @brief Шаг обновления по измерению угловой скорости рыскания.
   * H = [0, 0, 1, 0]
   * @param gz  Угловая скорость по Z от IMU [рад/с]
   */
  void UpdateGyroZ(float gz) noexcept;

  /**
   * @brief Шаг обновления по измерению курса от магнитометра.
   * H = [0, 0, 0, 1]. Инновация с обёрткой: atan2(sin(z-ψ), cos(z-ψ)).
   * @param heading_rad  Tilt-compensated heading [рад], любой диапазон
   */
  void UpdateHeading(float heading_rad) noexcept;

  /**
   * @brief Zero Velocity Update (ZUPT): псевдо-измерение vx=0, vy=0.
   * @param r_zupt  Шум псевдо-измерения [м²/с²]. По умолчанию 0.1.
   */
  void UpdateZeroVelocity(float r_zupt = 0.1f) noexcept;

  /**
   * @brief Измерение продольной скорости (мотор-модель): H = [1, 0, 0, 0].
   *
   * Якорь против дрейфа/разгона vx без датчика колёс. Ожидаемая скорость
   * оценивается из газа (v ≈ gain·throttle) и подаётся как СЛАБОЕ измерение
   * (большая r_speed): анкер удерживает vx в физических пределах, но не мешает
   * реальной динамике IMU. Реализация — скалярный Kalman-update по столбцу 0.
   * @param v_meas   Ожидаемая продольная скорость [м/с] (знаковая).
   * @param r_speed  Шум измерения [м²/с²]. Больше → слабее коррекция.
   */
  void UpdateSpeed(float v_meas, float r_speed) noexcept;

  /**
   * @brief Неголономное ограничение (NHC): псевдо-измерение vy = 0.
   *
   * Машина «не ездит боком» (кроме заноса): боковая скорость в СК кузова ≈ 0.
   * Честное измерение (в отличие от мягкого vy_decay в Predict) подавляет
   * накопление ошибки боковой скорости и стабилизирует slip angle. При заносе
   * ограничение ослабляют увеличением r_nhc (см. вызывающий код).
   * @param r_nhc  Шум псевдо-измерения [м²/с²].
   */
  void UpdateNonHolonomic(float r_nhc) noexcept;

  /**
   * @brief Высокоуровневое обновление из IMU (Predict + GyroZ + ZUPT).
   * @param ax_g Ускорение по X в g
   * @param ay_g Ускорение по Y в g
   * @param az_g Ускорение по Z в g
   * @param gz_dps Угловая скорость Z в °/с
   * @param dt_sec Шаг времени в секундах
   * @param throttle_abs Абсолютное значение throttle [0..1] для ZUPT gating.
   *        Если > kZuptThrottleThresh, ZUPT пропускается (машина пытается
   * ехать). По умолчанию 0 — ZUPT всегда активен (обратная совместимость).
   * @param pitch_rad Тангаж от фильтра ориентации [рад] (ZYX: вокруг оси Y).
   * @param roll_rad  Крен от фильтра ориентации [рад] (ZYX: вокруг оси X).
   *
   * Перед интеграцией из измеренного ускорения снимается проекция гравитации
   * по live-ориентации, так что в Predict уходит линейное ускорение «без g»
   * (см. контракт Predict). Вектор реакции гравитации в СК кузова (в g):
   *   grav_x = -sin(pitch),  grav_y = cos(pitch)·sin(roll).
   * По умолчанию pitch=roll=0 — компенсации нет (обратная совместимость).
   * ZUPT-гейт использует сырой модуль ускорения (|a|≈1g статически при любом
   * наклоне), поэтому компенсация на него не влияет.
   */
  void UpdateFromImu(float ax_g, float ay_g, float az_g, float gz_dps,
                     float dt_sec, float throttle_abs = 0.0f,
                     float pitch_rad = 0.0f, float roll_rad = 0.0f) noexcept;

  /** Результат ZUPT gate на последнем обновлении IMU. */
  [[nodiscard]] ZuptStatus GetZuptStatus() const noexcept {
    return zupt_status_;
  }

  // ─── Доступ к состоянию ───────────────────────────────────────────────

  /** Оценка продольной скорости [м/с]. */
  [[nodiscard]] float GetVx() const noexcept { return x_[0]; }

  /** Оценка боковой скорости [м/с]. */
  [[nodiscard]] float GetVy() const noexcept { return x_[1]; }

  /** Оценка угловой скорости рыскания [рад/с]. */
  [[nodiscard]] float GetYawRate() const noexcept { return x_[2]; }

  /** Оценка курсового угла [рад], диапазон [-π, π]. */
  [[nodiscard]] float GetYawRad() const noexcept { return x_[3]; }

  /** Оценка курсового угла [°], диапазон [-180, 180]. */
  [[nodiscard]] float GetYawDeg() const noexcept;

  /** Модуль скорости в горизонтальной плоскости [м/с]. */
  [[nodiscard]] float GetSpeedMs() const noexcept {
    return std::sqrt(x_[0] * x_[0] + x_[1] * x_[1]);
  }

  /** Угол заноса [рад]: β = atan2(vy, vx). При малой скорости — 0. */
  [[nodiscard]] float GetSlipAngleRad() const noexcept;

  /** Угол заноса [°]. При малой скорости — 0. */
  [[nodiscard]] float GetSlipAngleDeg() const noexcept;

  // ─── Неопределённость (диагональные элементы P) ──────────────────────

  /** Дисперсия оценки vx: P[0][0]. */
  [[nodiscard]] float GetVxVariance() const noexcept { return P_[0]; }

  /** Дисперсия оценки vy: P[1][1]. */
  [[nodiscard]] float GetVyVariance() const noexcept { return P_[5]; }

  /** Дисперсия оценки r: P[2][2]. */
  [[nodiscard]] float GetRVariance() const noexcept { return P_[10]; }

  /** Дисперсия оценки ψ: P[3][3]. */
  [[nodiscard]] float GetYawVariance() const noexcept { return P_[15]; }

  /** Установить параметры шума. */
  void SetNoiseParams(NoiseParams params) noexcept { params_ = params; }

  /**
   * @brief Сработал ли guard на расходимость в последнем цикле Predict.
   * true → оценка скорости/рыскания была ограничена физическим максимумом или
   * сброшена по NaN; потребителям (drive modes) отдаётся уже безопасное
   * значение, но факт расходимости виден в телеметрии.
   */
  [[nodiscard]] bool IsDiverged() const noexcept { return diverged_; }

  /** Последнее измерение скорости мотор-модели [м/с] (для телеметрии/тюнинга).
   */
  [[nodiscard]] float GetLastSpeedMeas() const noexcept {
    return last_speed_meas_;
  }

  /** Физический максимум |скорости| для guard [м/с]. */
  static constexpr float kMaxSpeedMs = 15.0f;
  /** Физический максимум |угловой скорости рыскания| для guard [рад/с]. */
  static constexpr float kMaxYawRateRps = 20.0f;

 private:
  // Вектор состояния: [vx, vy, r, ψ]
  float x_[4]{0.0f, 0.0f, 0.0f, 0.0f};

  // Матрица ковариации 4×4 (row-major: P_[i*4+j] = P[i][j])
  float P_[16]{};

  // Параметры шума
  NoiseParams params_;

  // Диагностика ZUPT; обновляется вместе с каждым UpdateFromImu().
  ZuptStatus zupt_status_{ZuptStatus::NotEvaluated};

  // Флаг срабатывания guard на расходимость в текущем тике
  // (сбрасывается в начале Predict, выставляется в GuardState).
  bool diverged_{false};

  // Последнее измерение скорости мотор-модели (для телеметрии).
  float last_speed_meas_{0.0f};

  // Вспомогательные методы
  void InitP() noexcept;

  /**
   * Guard на расходимость состояния x_: сброс к нулю при NaN/Inf и ограничение
   * |vx|,|vy| ≤ kMaxSpeedMs, |r| ≤ kMaxYawRateRps. Выставляет diverged_
   * (OR-семантика; пер-тиковый сброс флага — в начале Predict). Вызывается в
   * конце Predict и после измерительных апдейтов
   * (UpdateGyroZ/UpdateHeading/UpdateSpeed/UpdateNonHolonomic).
   */
  void GuardState() noexcept;

  /**
   * Обобщённое скалярное Kalman-обновление: измерение z по столбцу col,
   * H = e_col^T, инновация (z − x_[col]). Joseph form (PSD-гарантия).
   */
  void ScalarMeasUpdate(int col, float z, float r) noexcept;

  /**
   * Проверяет P на NaN/Inf (→ сброс к InitP) и ограничивает диагональные
   * элементы отрезком [kPDiagMin, kPDiagMax].
   */
  void ClampP() noexcept;

  /**
   * Скалярное Kalman-обновление с нулевым измерением: z = 0, H = e_col^T.
   * Joseph form гарантирует сохранение положительной полуопределённости P.
   */
  void ScalarZeroUpdate(int col, float r) noexcept;

  static void MatMul4x4(const float A[16], const float B[16],
                        float C[16]) noexcept;
  static void MatTranspose4x4(const float A[16], float At[16]) noexcept;
  static void SymmetrizeP(float P[16]) noexcept;

  /** Нормализация угла в [-π, π]. */
  static float WrapAngle(float a) noexcept;

  // Границы диагональных элементов P
  static constexpr float kPDiagMax = 1e3f;
  static constexpr float kPDiagMin = 1e-6f;
};

}  // namespace rc_vehicle
