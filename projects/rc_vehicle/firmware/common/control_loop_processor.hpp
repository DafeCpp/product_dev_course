#pragma once

#include <atomic>
#include <cstdint>

#include "auto_drive_coordinator.hpp"
#include "calibration_manager.hpp"
#include "control_components.hpp"
#include "control_loop_helpers.hpp"
#include "imu_calibration.hpp"
#include "kids_mode_processor.hpp"
#include "madgwick_filter.hpp"
#include "stabilization_manager.hpp"
#include "stabilization_pipeline.hpp"
#include "telemetry_manager.hpp"
#include "tilt_estimator.hpp"
#include "vehicle_control_platform.hpp"
#include "vehicle_ekf.hpp"

namespace rc_vehicle {

/**
 * @brief Контекст — ссылки на все подсистемы, нужные control loop.
 *
 * Заполняется VehicleControlUnified один раз перед запуском цикла
 * и передаётся в ControlLoopProcessor.
 */
struct ControlLoopContext {
  // Платформа и алгоритмические компоненты (всегда валидны)
  VehicleControlPlatform& platform;
  ImuCalibration& imu_calib;
  MadgwickFilter& madgwick;
  VehicleEkf& ekf;
  YawRateController& yaw_ctrl;
  PitchCompensator& pitch_ctrl;
  SlipAngleController& slip_ctrl;
  OversteerGuard& oversteer_guard;
  KidsModeProcessor& kids_processor;
  AutoDriveCoordinator& auto_drive;

  // Менеджеры (nullable: не созданы если IMU отсутствует)
  CalibrationManager* calib_mgr;
  StabilizationManager* stab_mgr;
  TelemetryManager* telem_mgr;

  // Handlers (nullable: rc/imu опциональны)
  RcInputHandler* rc_handler;
  WifiCommandHandler* wifi_handler;
  ImuHandler* imu_handler;
  TelemetryHandler* telem_handler;

  // Атомарный счётчик частоты (читается RunSelfTest из другого потока)
  std::atomic<uint32_t>& last_loop_hz;
};

/**
 * @brief Выполняет одну итерацию control loop.
 *
 * Инкапсулирует всё тело ControlTaskLoop (кроме while и watchdog).
 * VehicleControlUnified создаёт экземпляр и вызывает Step() каждый тик.
 */
class ControlLoopProcessor {
 public:
  ControlLoopProcessor(const ControlLoopContext& ctx, uint32_t now_ms)
      : ctx_(ctx),
        last_pwm_update_(now_ms),
        diag_start_ms_(now_ms) {}

  /** Выполнить одну итерацию. */
  void Step(uint32_t now, uint32_t dt_ms);

 private:
  void UpdateComponents(uint32_t now, uint32_t dt_ms);
  void UpdateSensorsAndEkf(uint32_t dt_ms);
  void UpdateAutoDrive(uint32_t now_ms, uint32_t dt_ms);
  void UpdateStabilization(uint32_t dt_ms);
  /** @return true — failsafe активен, PWM удерживается в нейтрали. */
  bool HandleFailsafe();
  void UpdatePwm(uint32_t now, uint32_t dt_ms);
  void UpdateTelemetry(uint32_t now, uint32_t dt_ms);

  const ControlLoopContext& ctx_;

  // Per-iteration mutable state
  float commanded_throttle_{0.0f};
  float commanded_steering_{0.0f};
  // Команда источника управления прошлого тика до Kids/stabilization. Это
  // независимый от speed limiter вход моторной модели EKF (LOS-246).
  float motor_model_throttle_{0.0f};
  float applied_throttle_{0.0f};
  float applied_steering_{0.0f};
  float prev_gz_rad_s_{0.0f};
  bool failsafe_was_active_{false};

  // Независимый от акселерометра источник тангажа/крена для grav-comp
  // (LOS-240). prev_vx_ хранит EKF vx предыдущего тика; a_lin_prev_g_ —
  // оценку продольного линейного ускорения (конечная разность), подаваемую
  // в TiltEstimator, НО ТОЛЬКО пока активен независимый якорь мотор-модели
  // (motor_model_enabled && !IsSpeedCalibActive() — см. UpdateSensorsAndEkf).
  // Без якоря a_lin принудительно 0.
  //
  // Была опробована и ОТБРОШЕНА идея «прогрева» (первые ~1с a_lin=0, потом
  // включать vx-обратную связь): при частичной (не полной) сходимости
  // тангажа к моменту включения feedback остаточная ошибка ВСЁ РАВНО
  // самоподтверждается на новом (частично неверном) уровне — прогрев лишь
  // ОТКЛАДЫВАЕТ уход vx в разнос, а не предотвращает его (corr_gain_hz=0.5
  // по умолчанию даёт ~63%-сходимость только за ~2с, полную — за секунды,
  // так что даже 1с прогрева оставляет существенный остаток). Проверено
  // эмпирически на сценарии код-ревью (PR #290, 4-й раунд): статический
  // наклон 20° без якоря/throttle>2% (ZUPT выключен) — vx уходил в -15
  // (клемп) и с прогревом, и без. Без якоря EKF vx — вообще не независимый
  // источник (сам зависит от текущего, возможно неверного, тангажа через
  // grav_x/grav_y в этом же UpdateFromImu) — обратная связь через него
  // структурно нестабильна на ЛЮБОМ уровне остаточной ошибки, не только при
  // старте с нуля. Мотор-модель — единственный ДЕЙСТВИТЕЛЬНО независимый
  // (от IMU/EKF/тангажа) сигнал в системе; без неё TiltEstimator честно
  // деградирует до гиро + негейтированной accel-коррекции (медленнее и
  // менее точно при устойчивом разгоне, но СТАБИЛЬНО — не расходится).
  // true, когда tilt_est_.Update() вызывался на ПРЕДЫДУЩЕМ тике — ловит
  // ЛЮБОЕ возобновление после перерыва в рантайме (код-ревью PR #290, 9-й
  // и 10-й раунды): tilt_comp_enabled=false, ekf_enabled=false,
  // imu_enabled=false или dt_ms==0 — во всех случаях Update() не
  // вызывается, а внутренние pitch_rad_/roll_rad_ остаются ЗАМОРОЖЕНЫ на
  // последнем значении. Без сброса при возобновлении EKF на первом же тике
  // получил бы протухший тангаж/крен из интервала простоя — до нескольких
  // секунд ложной grav-компенсации при corr_gain_hz по умолчанию
  // (аналогично сбросу на смену СК от калибровки, 5-й раунд, но здесь
  // триггер — любой перерыв в вызовах Update(), а не конкретный флаг).
  // Устанавливается БЕЗУСЛОВНО в конце UpdateSensorsAndEkf() — см. там.
  bool tilt_was_enabled_{false};
  TiltEstimator tilt_est_;
  float prev_vx_{0.0f};
  float a_lin_prev_g_{0.0f};
  uint32_t last_pwm_update_;
  uint32_t diag_loop_count_{0};
  uint32_t diag_start_ms_;

#ifdef RC_PROFILE_LOOP
  // FW-R16: профилирование стадий итерации (debug-сборка). Накапливаем мкс по
  // стадиям и раз в диаг-интервал печатаем средние us/iter. Включается флагом
  // -DRC_PROFILE_LOOP=1; в обычной сборке кода нет (нулевой оверхед).
  void EmitProfile(uint32_t loops);
  uint64_t prof_components_us_{0};
  uint64_t prof_sensors_us_{0};
  uint64_t prof_control_us_{0};
  uint64_t prof_stab_us_{0};
  uint64_t prof_pwm_us_{0};
  uint64_t prof_telem_us_{0};
#endif

  // Кэшированный снимок датчиков (обновляется в UpdateSensorsAndEkf)
  SensorSnapshot sensors_;
  StabilizationConfig stab_cfg_;
};

}  // namespace rc_vehicle
