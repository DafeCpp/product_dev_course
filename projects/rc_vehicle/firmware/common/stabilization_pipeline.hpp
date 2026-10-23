#pragma once

#include <firmware_common/pid_controller.hpp>

#include "control_components.hpp"
#include "control_tick.hpp"
#include "drive_mode_strategy.hpp"
#include "kids_mode_processor.hpp"
#include "madgwick_filter.hpp"
#include "stabilization_config.hpp"
#include "vehicle_ekf.hpp"

namespace rc_vehicle {

/** Snapshot consumed by the stabilization pipeline for one control tick. */
struct StabilizationInput {
  ControlSetpoint command{};
  uint32_t dt_ms{0};
  float speed_ms{0.0f};
  /** Продольная скорость EKF со знаком [м/с], < 0 — назад (LOS-386). */
  float vx_ms{0.0f};
  float slip_angle_deg{0.0f};
  float yaw_rate_rps{0.0f};
  float vx_variance{0.0f};
  float filtered_gyro_z_dps{0.0f};
  float pitch_deg{0.0f};
  /** Крен от TiltEstimator [°], > 0 — правый борт ниже (LOS-386). */
  float roll_deg{0.0f};
  float forward_accel_g{0.0f};
  /** Пробуксовка колёс и скорость корпуса без неё (LOS-385). */
  float wheel_slip_ratio{0.0f};
  float ground_speed_ms{0.0f};
  bool wheels_driven{false};
  /** Yaw rate разошёлся с рулём: шины на пределе (LOS-378). */
  bool lateral_grip_lost{false};
  /** Боковое ускорение кузова без гравитации [g], >0 — влево. */
  float lateral_accel_g{0.0f};
  /** lateral_accel_g без гравитации от TiltEstimator: Madgwick в повороте
   *  принимает боковое ускорение за крен и вычитает его. */
  bool lateral_accel_valid{false};
  float stabilization_weight{0.0f};
  float mode_transition_weight{0.0f};
  bool imu_enabled{false};
  bool ekf_diverged{false};
  bool speed_calibration_active{false};
};

/** Command and motor-model target produced by stabilization for this tick. */
struct StabilizationOutput {
  ControlSetpoint command{};
  float motor_model_target_throttle{0.0f};
  /** Oversteer guard режет газ: снижение проходит мимо slew rate, иначе
   *  задняя ось разворачивает машину раньше, чем газ успеет упасть
   *  (LOS-380). */
  bool bypass_throttle_slew_down{false};
};

// ═════════════════════════════════════════════════════════════════════════════
// YawRateController
// ═════════════════════════════════════════════════════════════════════════════

/**
 * @brief ПИД-регулятор угловой скорости рыскания (yaw rate) с адаптивным
 *        масштабированием по скорости из EKF.
 *
 * Активен в режимах Normal (0) и Sport (1). В Drift mode (2) yaw PID
 * отключён — управление рулём остаётся за водителем, а стабилизацией
 * заноса занимается SlipAngleController.
 *
 * Извлечён из VehicleControlUnified::ControlTaskLoop() (строки 154–173).
 */
class YawRateController {
 public:
  YawRateController() = default;

  /**
   * @brief Инициализация: привязать зависимости и установить начальные PID
   *        коэффициенты из конфигурации.
   * @param cfg  Конфигурация стабилизации (хранится по ссылке — читается live)
   * @param ekf  EKF для оценки скорости (адаптивный PID)
   * @param imu  IMU handler (может быть nullptr если IMU не включён)
   */
  void Init(const StabilizationConfig& cfg, const VehicleEkf& ekf,
            const ImuHandler* imu);

  /**
   * @brief Один шаг yaw rate PID.
   * @param cfg              Живой снимок конфига текущей итерации (FW-R23)
   * @param steering         Команда руля [in/out], корректируется в
   * normal/sport
   * @param stab_w           Вес стабилизации [0..1]
   * @param mode_w           Вес перехода между режимами [0..1]
   * @param dt_ms            Шаг времени в миллисекундах
   * @param reversing        true — машина едет назад (команда газа < 0):
   *                         yaw-rate стабилизация отключается (FW-R22, иначе
   *                         автоколебания руля)
   */
  void Process(const StabilizationConfig& cfg, float& steering, float stab_w,
               float mode_w, uint32_t dt_ms, bool reversing = false) noexcept;

  /** Snapshot-based production path.
   *  @param ref_cutoff_hz Частота среза опорной модели уставки
   *                       (ModeTraits::yaw_ref_cutoff_hz), 0 — без фильтра.
   */
  void Process(const StabilizationConfig& cfg, float& steering,
               const StabilizationInput& input, bool reversing = false,
               float ref_cutoff_hz = 0.0f) noexcept;

  /**
   * @brief Обновить PID-коэффициенты из конфигурации.
   * @param cfg Новая конфигурация
   */
  void SetGains(const StabilizationConfig& cfg) noexcept;

  /** @brief Сбросить PID, фильтр ошибки и состояние скоростного гейта. */
  void Reset() noexcept;

  /** @brief Доступ к PID (для тестирования). */
  [[nodiscard]] const firmware_common::PidController& GetPid() const noexcept {
    return pid_;
  }

 private:
  const VehicleEkf* ekf_{nullptr};
  const ImuHandler* imu_{nullptr};
  firmware_common::PidController pid_;
  float filtered_error_dps_{0.0f};
  float omega_ref_dps_{0.0f};
  bool omega_ref_valid_{false};
  float activation_weight_{0.0f};
  bool speed_gate_active_{false};
};

// ═════════════════════════════════════════════════════════════════════════════
// PitchCompensator
// ═════════════════════════════════════════════════════════════════════════════

/**
 * @brief Компенсация наклона: коррекция газа по углу pitch (стабилизация на
 *        склонах).
 *
 * В прошивке pitch > 0 — нос ВНИЗ (Madgwick/TiltEstimator, LOS-369):
 * подъём (pitch < 0) → увеличить газ, спуск (pitch > 0) → уменьшить.
 * При нейтральном газе не действует: отпущенный стик не должен сам
 * толкать машину. До |throttle| = 0.2 коррекция нарастает линейно, знак
 * команды она не меняет. Коррекция ограничена pitch_comp_max_correction.
 *
 * Извлечён из VehicleControlUnified::ControlTaskLoop() (строки 180–191).
 * Fix #8 (REFACTORING.md): ручной if/else заменён на std::clamp.
 */
class PitchCompensator {
 public:
  PitchCompensator() = default;

  /**
   * @brief Инициализация: привязать зависимости.
   * @param madgwick Фильтр ориентации для получения pitch
   * @param imu      IMU handler (nullptr — компенсация не работает)
   */
  void Init(const MadgwickFilter& madgwick, const ImuHandler* imu);

  /**
   * @brief Применить pitch-компенсацию к газу.
   * @param cfg       Живой снимок конфига текущей итерации (FW-R23)
   * @param throttle  Команда газа [in/out]
   * @param stab_w    Вес стабилизации [0..1]
   */
  void Process(const StabilizationConfig& cfg, float& throttle,
               float stab_w) noexcept;

  /** Snapshot-based production path. */
  void Process(const StabilizationConfig& cfg, float& throttle,
               const StabilizationInput& input) noexcept;

 private:
  const MadgwickFilter* madgwick_{nullptr};
  const ImuHandler* imu_{nullptr};
};

// ═════════════════════════════════════════════════════════════════════════════
// RollCompensator
// ═════════════════════════════════════════════════════════════════════════════

/**
 * @brief Компенсация крена: руль вверх по поперечному склону (LOS-386).
 *
 * Боковая составляющая тяжести на склоне нагружает шины как поворот, и
 * машина с недостаточной поворачиваемостью уходит вниз по склону. Добавка
 * руля correction = clamp(gain·крен, ±max_correction) упреждает её; в
 * режимах с yaw-rate ПИД она снимает с интегратора догоняющую работу, без
 * него (Drift) — единственное, что держит курс.
 *
 * Крен > 0 — правый борт ниже (TiltEstimator: roll = atan2(ay, az)), вверх
 * по склону — влево, т.е. руль > 0. Компенсация не действует:
 *  - на малой скорости (нарастает от kMinSpeedMs до kFullSpeedMs): на месте
 *    руль сам по себе не должен шевелиться;
 *  - на заднем ходу (команда газа или vx EKF < 0; speed_ms — модуль, назад
 *    не отличает): роли осей меняются, знак поправки не определён;
 *  - в повороте: крен корпуса от центробежной силы TiltEstimator не отличает
 *    от склона, и добавка руля внутрь поворота его бы закручивала. Поэтому
 *    вес падает до нуля между kFullLatG и kZeroLatG по |v·r|.
 * Как и ПИД-регуляторы, масштабируется на stab_w и mode_transition_weight:
 * при смене профиля с другим gain руль не прыгает.
 */
class RollCompensator {
 public:
  static constexpr float kMinSpeedMs = 0.3f;
  static constexpr float kFullSpeedMs = 1.0f;
  static constexpr float kFullLatG = 0.05f;
  static constexpr float kZeroLatG = 0.10f;

  /**
   * @brief Применить roll-компенсацию к рулю.
   * @param cfg      Живой снимок конфига текущей итерации
   * @param steering Команда руля [in/out]
   * @param input    Снимок состояния тика
   */
  void Process(const StabilizationConfig& cfg, float& steering,
               const StabilizationInput& input) noexcept;

  /** Добавка руля на последнем Process() (0 — не действовала). */
  [[nodiscard]] float GetLastCorrection() const noexcept {
    return last_correction_;
  }

 private:
  float last_correction_{0.0f};
};

// ═════════════════════════════════════════════════════════════════════════════
// SlipAngleController
// ═════════════════════════════════════════════════════════════════════════════

/**
 * @brief ПИД-регулятор угла заноса (slip angle) для поддержки дрифта.
 *
 * Активен только в Drift mode (mode=2) при включённом IMU.
 * Корректирует газ для поддержания целевого угла заноса из EKF.
 *
 * Извлечён из VehicleControlUnified::ControlTaskLoop() (строки 197–207).
 */
class SlipAngleController {
 public:
  SlipAngleController() = default;

  /**
   * @brief Инициализация: привязать зависимости и установить начальные PID
   *        коэффициенты из конфигурации.
   * @param cfg  Конфигурация стабилизации
   * @param ekf  EKF для получения текущего угла заноса
   * @param imu  IMU handler (nullptr — PID не работает)
   */
  void Init(const StabilizationConfig& cfg, const VehicleEkf& ekf,
            const ImuHandler* imu);

  /**
   * @brief Один шаг slip angle PID (только в drift mode).
   * @param cfg       Живой снимок конфига текущей итерации (FW-R23)
   * @param throttle  Команда газа [in/out], корректируется в режиме drift
   * @param stab_w    Вес стабилизации [0..1]
   * @param mode_w    Вес перехода между режимами [0..1]
   * @param dt_ms     Шаг времени в миллисекундах
   */
  void Process(const StabilizationConfig& cfg, float& throttle, float stab_w,
               float mode_w, uint32_t dt_ms) noexcept;

  /** Snapshot-based production path. */
  void Process(const StabilizationConfig& cfg, float& throttle,
               const StabilizationInput& input) noexcept;

  /**
   * @brief Обновить PID-коэффициенты из конфигурации.
   * @param cfg Новая конфигурация
   */
  void SetGains(const StabilizationConfig& cfg) noexcept;

  /**
   * @brief Угол заноса относительно поворота [°]: > 0 — зад выносит наружу.
   *
   * slip EKF знаковый: в левом повороте (yaw rate > 0) занос даёт slip < 0,
   * в правом — slip > 0. target_deg задаёт величину заноса в любую сторону;
   * со знаковым slip в левом повороте ошибка уставки росла с заносом, и PID
   * добавлял газ до разворота (LOS-380).
   */
  [[nodiscard]] static float DriftSlipDeg(float slip_deg,
                                          float yaw_rate_rps) noexcept;

  /// Ниже этого |yaw rate| поворота нет (как в OversteerGuard, ~17 °/с).
  static constexpr float kMinTurnYawRateRps = 0.3f;

  /** @brief Сбросить интегратор и историю PID. */
  void Reset() noexcept { pid_.Reset(); }

  /** @brief Доступ к PID (для тестирования). */
  [[nodiscard]] const firmware_common::PidController& GetPid() const noexcept {
    return pid_;
  }

 private:
  const VehicleEkf* ekf_{nullptr};
  const ImuHandler* imu_{nullptr};
  firmware_common::PidController pid_;
};

// ═════════════════════════════════════════════════════════════════════════════
// OversteerGuard
// ═════════════════════════════════════════════════════════════════════════════

/**
 * @brief Обнаружение заноса (oversteer prediction) и опциональное снижение
 * газа.
 *
 * Срабатывает когда |slip_angle| > thresh_slip И |d(slip)/dt| > thresh_rate.
 * В режимах Normal/Sport снижает газ на oversteer_throttle_reduction.
 * В Drift mode снижение газа отключено (занос ожидается и желателен).
 *
 * Извлечён из VehicleControlUnified::ControlTaskLoop() (строки 213–227).
 * Владеет prev_slip_deg_ и oversteer_active_ (перенесены из VCU).
 */
class OversteerGuard {
 public:
  OversteerGuard() = default;

  /**
   * @brief Инициализация: привязать зависимости.
   * @param ekf  EKF для получения угла заноса и его производной
   * @param imu  IMU handler (nullptr — guard не работает)
   */
  void Init(const VehicleEkf& ekf, const ImuHandler* imu);

  /**
   * @brief Один шаг oversteer detection.
   * @param cfg              Живой снимок конфига текущей итерации (FW-R23)
   * @param throttle         Команда газа [in/out], может быть снижена при
   *                         oversteer
   * @param dt_ms            Шаг времени в миллисекундах
   * @param reduce_throttle  Разрешено ли снижение газа (определяется
   * ModeTraits)
   */
  void Process(const StabilizationConfig& cfg, float& throttle, uint32_t dt_ms,
               bool reduce_throttle = true) noexcept;

  /** Snapshot-based production path. */
  void Process(const StabilizationConfig& cfg, float& throttle,
               const StabilizationInput& input,
               bool reduce_throttle = true) noexcept;

  /** @brief Сбросить состояние (вызывается при failsafe). */
  void Reset() noexcept;
  /** Снять срез и потолок газа, не трогая prev_slip_deg_: гард выключен или
   *  не вызывается, и старое вмешательство не должно ожить при включении. */
  void ReleaseCut() noexcept;

  /** @brief Газ срезан на этом тике (флаг и разрешённое снижение). */
  [[nodiscard]] bool IsCuttingThrottle() const noexcept {
    return cutting_throttle_;
  }

  /** @brief Текущий флаг срабатывания oversteer. */
  [[nodiscard]] bool IsActive() const noexcept { return oversteer_active_; }

  /**
   * @brief Указатель на флаг oversteer для TelemetryHandler::SetOversteerWarn.
   * @return Указатель на oversteer_active_
   */
  [[nodiscard]] const bool* GetActivePtr() const noexcept {
    return &oversteer_active_;
  }

 private:
  const VehicleEkf* ekf_{nullptr};
  const ImuHandler* imu_{nullptr};

  float prev_slip_deg_{0.0f};  ///< Предыдущий угол заноса для оценки dslip/dt
  bool oversteer_active_{false};  ///< Текущее состояние oversteer detection
  bool cutting_throttle_{false};
  /** Сколько кузов уже вращается быстрее траектории [с], в
   *  [0, kRotationExcessHoldS]. */
  float rotation_excess_time_s_{0.0f};
  /** Сколько занос уже гаснет [с]; на kReleaseCalmS guard отпускает. */
  float calm_time_s_{0.0f};
  /** Потолок газа после заноса; 1 — не ограничивает. */
  float throttle_ceiling_{1.0f};

 public:
  /**
   * Насколько yaw rate может превышать скорость поворота траектории ay/vx
   * [рад/с]. Их разность — скорость роста угла заноса −β̇: пока шины держат,
   * она около нуля, а при срыве задней оси растёт раньше самого угла. Порог
   * угла заноса EKF (5°) машина проходит на ~120 мс позже, и на μ 0.3 задняя
   * ось к тому времени уже разворачивает её (LOS-380).
   */
  static constexpr float kRotationExcessRps = 0.35f;  // ≈ 20 °/с
  /** Сколько превышение должно держаться [с]: выбросы гироскопа — не занос. */
  static constexpr float kRotationExcessHoldS = 0.03f;
  /** Сколько занос должен быть погашен [с], чтобы guard отпустил газ. */
  static constexpr float kReleaseCalmS = 0.2f;
  /** Скорость возврата газа после заноса [1/с]. */
  static constexpr float kCeilingRecoveryPerS = 0.25f;
};

/**
 * Ordered, mode-agnostic stabilization computation.
 *
 * Drive-mode selection happens outside this class. The pipeline receives only
 * declarative ModeTraits and value snapshots; it performs no platform I/O.
 */
class StabilizationPipeline {
 public:
  StabilizationPipeline(YawRateController& yaw_ctrl,
                        PitchCompensator& pitch_ctrl,
                        SlipAngleController& slip_ctrl,
                        OversteerGuard& oversteer_guard,
                        KidsModeProcessor& kids_processor) noexcept
      : yaw_ctrl_(yaw_ctrl),
        pitch_ctrl_(pitch_ctrl),
        slip_ctrl_(slip_ctrl),
        oversteer_guard_(oversteer_guard),
        kids_processor_(kids_processor) {}

  [[nodiscard]] StabilizationOutput Process(
      const StabilizationConfig& cfg, const ModeTraits& policy,
      const StabilizationInput& input) noexcept;

  /** Компенсатор крена (без зависимостей, поэтому принадлежит конвейеру). */
  [[nodiscard]] const RollCompensator& GetRollCompensator() const noexcept {
    return roll_ctrl_;
  }

 private:
  YawRateController& yaw_ctrl_;
  PitchCompensator& pitch_ctrl_;
  SlipAngleController& slip_ctrl_;
  OversteerGuard& oversteer_guard_;
  KidsModeProcessor& kids_processor_;
  RollCompensator roll_ctrl_;
};

}  // namespace rc_vehicle
