#pragma once

namespace rc_vehicle {

/**
 * @brief Признак потери бокового сцепления по рулю и yaw rate (LOS-378).
 *
 * Пока шины держат, машина поворачивает так, как велит руль: установившийся
 * yaw rate велосипедной модели r_ref = vx·tan(δ) / (L + K·vx²). Когда шины
 * упираются в предел сцепления, yaw rate расходится с r_ref: при сносе
 * меньше, при заносе задней оси больше. В отличие от интеграла бокового
 * ускорения, этот признак не копит шум дороги: гироскоп, руль и vx почти не
 * шумят.
 *
 * VehicleEkf гасит боковую скорость, пока шины держат (vy_decay_hz), и
 * перестаёт гасить, когда сцепление потеряно: тогда vy — честный интеграл
 * ay − r·vx, и угол заноса EKF не застревает на единицах градусов.
 *
 * Геометрия взята из профиля fitted_2026_08_02 (SIL); K = 0 — нейтральная
 * поворачиваемость, как в этом профиле. На машине K и угол руля надо
 * подогнать по кругу на асфальте (протокол, п. 7.5f): при недостаточной
 * поворачиваемости r на асфальте ниже r_ref, и признак срабатывал бы ложно.
 */
class GripLossDetector {
 public:
  void Reset() noexcept;

  /**
   * @brief Один тик control loop.
   * @param yaw_rate_rps  Измеренный yaw rate [рад/с], >0 — влево.
   * @param vx_ms         Продольная скорость [м/с].
   * @param steering      Руль, поданный на серво, в [-1, 1]; >0 — влево.
   * @param dt_sec        Шаг [с]; при dt_sec <= 0 состояние не меняется.
   */
  void Update(float yaw_rate_rps, float vx_ms, float steering,
              float dt_sec) noexcept;

  [[nodiscard]] bool IsGripLost() const noexcept { return grip_lost_; }

  /**
   * @brief Руль в [0, 1], при котором шины дали бы этот |yaw rate|.
   *
   * Обратная к r_ref: tan(δ) = |r|·(L + K·vx²) / vx. При vx <= 0 — 1
   * (не ограничивает).
   */
  [[nodiscard]] static float SteeringForYawRate(float yaw_rate_rps,
                                                float vx_ms) noexcept;

  /** Yaw rate, который дал бы руль при сцеплении, с запаздыванием
   *  kYawResponseTauS [рад/с]. */
  [[nodiscard]] float GetReferenceYawRateRps() const noexcept {
    return reference_yaw_rate_rps_;
  }

  /** Колёсная база [м]. */
  static constexpr float kWheelbaseM = 0.30f;
  /** Угол колёс при руле ±1 [рад] (20.4°). */
  static constexpr float kMaxSteerRad = 0.356047f;
  /** Градиент недостаточной поворачиваемости K [с²/м]: знаменатель L + K·vx².
   */
  static constexpr float kUndersteerGradient = 0.0f;
  /**
   * Допуск расхождения r и r_ref: доля r_ref и не меньше kMinYawDevRps.
   * На асфальте в SIL расхождение 2–3 %, в заносе на скользком — 25 %.
   */
  static constexpr float kMaxYawDevRatio = 0.15f;
  static constexpr float kMinYawDevRps = 0.15f;
  /**
   * Сколько расхождение должно держаться [с]: одиночные выбросы гироскопа
   * и руля — ещё не срыв.
   */
  static constexpr float kHoldS = 0.15f;
  /**
   * Постоянная времени отклика yaw rate на руль [с]: эталон сравнивается с
   * задержкой. Пока руль поворачивается со slew rate, yaw rate отстаёт от
   * установившегося на ~50 мс (инерция кузова, фильтр гироскопа), и без
   * задержки в SIL на асфальте вход в поворот выглядел как срыв.
   */
  static constexpr float kYawResponseTauS = 0.06f;
  /** Ниже этой скорости вперёд признак не считается [м/с]. */
  static constexpr float kMinSpeedMs = 0.5f;

 private:
  float reference_yaw_rate_rps_{0.0f};
  // Накопленное время расхождения в [0, kHoldS]: копится при расхождении и
  // убывает без него, так что одиночные выбросы не переключают признак.
  float deviation_time_s_{0.0f};
  bool grip_lost_{false};
  // Эталон уже инициализирован установившимся значением (после Reset — нет).
  bool primed_{false};
};

}  // namespace rc_vehicle
