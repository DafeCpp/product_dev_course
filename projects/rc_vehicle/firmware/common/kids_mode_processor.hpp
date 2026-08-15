#pragma once

#include "control_components.hpp"
#include "stabilization_config.hpp"
#include "vehicle_ekf.hpp"

namespace rc_vehicle {

struct StabilizationInput;

/**
 * @brief Процессор детского режима (Kids Mode)
 *
 * Применяет ограничения газа и руля, а также усиленную защиту от заноса
 * (anti-spin). Slew rate применяется единым каскадом в PWM-пути; поля
 * kids_mode.slew_* задают его безопасный верхний предел.
 *
 * Конфиг передаётся в Process()/IsActive() аргументом — живой per-tick снимок
 * из control loop (FW-R21). Процессор НЕ хранит указатель на конфиг: при старте
 * режим обычно ещё не Kids, а переключение режима/пресета происходит в
 * рантайме, поэтому единственный корректный источник — снимок текущей итерации.
 * Control loop вызывает Process() только когда ModeTraits.apply_input_limits ==
 * true, но внутренняя проверка IsActive(cfg) несёт собственную семантику:
 * ModeTraits статичны по режиму и не видят рантайм-флаг
 * kids_mode.limiters_enabled, поэтому именно IsActive() отключает ограничители
 * при снятом мастер-выключателе (LOS-286).
 */
class KidsModeProcessor {
 public:
  KidsModeProcessor() = default;

  /**
   * @brief Инициализация процессора
   * @param ekf EKF для получения угла заноса (anti-spin) и скорости
   * @param imu IMU handler (может быть nullptr если IMU не включён)
   */
  void Init(const VehicleEkf& ekf, const ImuHandler* imu);

  /**
   * @brief Применить ограничения Kids Mode
   * @param cfg Живой снимок конфига стабилизации текущей итерации
   * @param throttle Команда газа [in/out]
   * @param steering Команда руля [in/out]
   * @param dt_ms Шаг времени в миллисекундах
   * @param forward_accel Продольное ускорение IMU [g] для accel limiter
   * @param throttle_before_speed_limit Не-null: получает throttle после
   *        обычных Kids-ограничений, но до speed limiter (LOS-246)
   * @param apply_speed_limit Применить speed limiter сразу; control loop
   *        передаёт false, чтобы применить его после остальных стабилизаторов
   */
  void Process(const StabilizationConfig& cfg, float& throttle, float& steering,
               uint32_t dt_ms, float forward_accel = 0.0f,
               float* throttle_before_speed_limit = nullptr,
               bool apply_speed_limit = true) noexcept;

  /** Snapshot-based production path. */
  void Process(const StabilizationConfig& cfg, float& throttle, float& steering,
               const StabilizationInput& input,
               float* throttle_before_speed_limit = nullptr,
               bool apply_speed_limit = true) noexcept;

  /**
   * @brief Применить feedback speed limiter к фактической команде.
   *
   * dt_ms обязателен (без дефолта): при выключенной мотор-модели адаптивный
   * потолок speed_trim_ обновляется только когда dt_sec > 0 (см.
   * ApplySpeedLimit() в .cpp) — молчаливый dt_ms=0 оставлял бы cap=1.0
   * (не ограничивает), при этом speed_limit_active_ всё равно выставлялся бы
   * в true, так как гистерезис зависит только от speed, не от dt.
   */
  void ApplySpeedLimit(const StabilizationConfig& cfg, float& throttle,
                       uint32_t dt_ms) noexcept;

  /** Snapshot-based production path. */
  void ApplySpeedLimit(const StabilizationConfig& cfg, float& throttle,
                       const StabilizationInput& input) noexcept;

  /**
   * @brief Проверить, применяются ли ограничители Kids для переданного конфига
   *
   * Делегирует единому предикату StabilizationConfig::KidsLimitersActive():
   * нужен и режим Kids, и включённый мастер-выключатель kids_mode
   * .limiters_enabled. Проверка НЕ смотрит на cfg.enabled — тот гасит только
   * контуры стабилизации (LOS-286).
   *
   * @return true если ограничители Kids должны применяться
   */
  [[nodiscard]] bool IsActive(const StabilizationConfig& cfg) const noexcept {
    return cfg.KidsLimitersActive();
  }

  /**
   * @brief Проверить, сработала ли защита anti-spin
   * @return true если anti-spin активен
   */
  [[nodiscard]] bool IsAntiSpinActive() const noexcept {
    return anti_spin_active_;
  }

  /**
   * @brief Проверить, сработало ли ограничение по ускорению
   * @return true если accel limiter снижает throttle
   */
  [[nodiscard]] bool IsAccelLimitActive() const noexcept {
    return accel_limit_active_;
  }

  /**
   * @brief Проверить, сработало ли ограничение по скорости (EKF)
   * @return true если speed limiter снижает throttle
   */
  [[nodiscard]] bool IsSpeedLimitActive() const noexcept {
    return speed_limit_active_;
  }

  /**
   * @brief Сбросить состояние процессора
   */
  void Reset() noexcept;

 private:
  const VehicleEkf* ekf_{nullptr};
  const ImuHandler* imu_{nullptr};

  bool anti_spin_active_{false};
  bool accel_limit_active_{false};
  bool speed_limit_active_{false};

  /**
   * Адаптивный потолок газа speed limiter'а [0..1], 1.0 = не ограничен.
   * Используется только когда мотор-модель выключена (filter
   * .motor_model_enabled == false) — тогда EKF speed_ms честная (пусть и
   * дрейфующая) IMU-интеграция, и есть смысл медленно подстраивать под неё
   * потолок (LOS-285). Пока мотор-модель включена, ограничение целиком
   * детерминированное (см. ApplySpeedLimit()) и это поле не трогается.
   */
  float speed_trim_{1.0f};
};

}  // namespace rc_vehicle
