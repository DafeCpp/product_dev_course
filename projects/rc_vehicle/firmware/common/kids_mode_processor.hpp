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
   * @brief Ограничить срез газа при заносе снизу газом наката (LOS-389).
   *
   * Ниже газа, при котором мотор держит текущую скорость, противо-ЭДС
   * тормозит ведущие колёса, и занос переходит в разворот. Газ вперёд
   * режется до max(reduced, накат), но не выше commanded; реверс и
   * недоверенная скорость EKF — без пола. Пока колёса буксуют, накат
   * считается по скорости корпуса: скорость EKF идёт за колёсами (LOS-385).
   */
  [[nodiscard]] static float FloorSkidCut(const StabilizationConfig& cfg,
                                          const StabilizationInput& input,
                                          float commanded,
                                          float reduced) noexcept;

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
   * @brief Пробуксовка колёс держит anti-spin включённым (LOS-385)
   *
   * Срабатывает выше kWheelspinOnRatio, пока мотор разгоняет колёса
   * (StabilizationInput::wheels_driven), отпускает ниже kWheelspinOffRatio:
   * после среза газа колёса догоняют корпус, и без гистерезиса anti-spin
   * включался бы и выключался каждые несколько тиков.
   */
  [[nodiscard]] bool IsWheelspinActive() const noexcept {
    return wheelspin_active_;
  }

  /**
   * @brief Руль ограничен по сцеплению (LOS-378)
   *
   * Пока yaw rate расходится с рулём (StabilizationInput::lateral_grip_lost),
   * потолок руля опускается до kGripSteerYawRatio от руля, который дал бы
   * измеренный yaw rate: передняя ось выходит из насыщения, и машина
   * поворачивает на пределе сцепления, а не скользит. Газом медленный занос не
   * уменьшить: ниже наката противо-ЭДС тормозит заднюю ось, и занос только
   * растёт (LOS-389).
   */
  [[nodiscard]] bool IsGripSteerLimitActive() const noexcept {
    return grip_steer_limit_active_;
  }

  /**
   * @brief Повторно ограничить руль потолком по сцеплению этого тика.
   *
   * Yaw-PID в Kids работает после Process() и добавляет коррекцию к уже
   * ограниченному рулю; без повторного ограничения он вернул бы шины в
   * насыщение (LOS-378). Руль против вращения (контрруль) не ограничивается.
   */
  void ClampToGripSteerCap(float& steering) noexcept;

  /** Доля руля от соответствующего измеренному yaw rate, до которой
   *  опускается потолок. При 1.0 передняя ось остаётся в насыщении, и
   *  занос в SIL растёт дальше. */
  static constexpr float kGripSteerYawRatio = 0.9f;
  /** Скорость возврата потолка руля, пока сцепление есть [1/с]: плавно, иначе
   *  полный руль сразу снова срывал бы шины. */
  static constexpr float kGripSteerRecoveryPerS = 0.5f;

  /** Доля пробуксовки колёс на последнем Process() (LOS-385). */
  [[nodiscard]] float GetWheelSlipRatio() const noexcept {
    return wheel_slip_ratio_;
  }

  /** Порог включения anti-spin по пробуксовке колёс (LOS-385). На асфальте
   *  в SIL доля не выше 0.08, на льду при старте доходит до 0.2. */
  static constexpr float kWheelspinOnRatio = 0.12f;
  /** Порог выключения: ниже него колёса считаются догнавшими корпус. */
  static constexpr float kWheelspinOffRatio = 0.05f;

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
  void ApplyGripSteerLimit(const StabilizationConfig& cfg, float& steering,
                           const StabilizationInput& input) noexcept;

  const VehicleEkf* ekf_{nullptr};
  const ImuHandler* imu_{nullptr};

  bool anti_spin_active_{false};
  bool accel_limit_active_{false};
  bool speed_limit_active_{false};
  bool wheelspin_active_{false};
  float wheel_slip_ratio_{0.0f};
  bool grip_steer_limit_active_{false};
  // Потолок |руля| по сцеплению [0..1], 1.0 = не ограничен (LOS-378).
  float grip_steer_cap_{1.0f};
  // Знак yaw rate этого тика: потолок держит только руль в ту же сторону.
  float yaw_sign_{0.0f};

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
