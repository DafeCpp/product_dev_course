#pragma once

#include <atomic>
#include <cstdint>

#include "auto_drive_coordinator.hpp"
#include "calibration_manager.hpp"
#include "control_components.hpp"
#include "control_loop_helpers.hpp"
#include "control_tick.hpp"
#include "imu_calibration.hpp"
#include "kids_mode_processor.hpp"
#include "madgwick_filter.hpp"
#include "stabilization_manager.hpp"
#include "stabilization_pipeline.hpp"
#include "telemetry_manager.hpp"
#include "vehicle_control_platform.hpp"
#include "vehicle_ekf.hpp"
#include "vehicle_state_estimator.hpp"

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
        state_estimator_(ctx.imu_calib, ctx.madgwick, ctx.ekf),
        diag_start_ms_(now_ms) {
    persistent_.last_pwm_update = now_ms;
  }

  /** Выполнить одну итерацию. */
  void Step(uint32_t now, uint32_t dt_ms);

 private:
  void UpdateComponents(uint32_t now, uint32_t dt_ms);
  void UpdateSensorsAndEkf(ControlTickInput& input, ControlTickState& state);
  void ProcessCalibration(uint32_t now_ms, ControlTickState& state);
  void UpdateAutoDrive(const ControlTickInput& input, ControlTickState& state);
  void UpdateStabilization(const ControlTickInput& input,
                           ControlTickState& state);
  /** @return true — failsafe активен, PWM удерживается в нейтрали. */
  bool HandleFailsafe(const ControlTickInput& input, ControlTickState& state);
  void UpdatePwm(const ControlTickInput& input, ControlTickState& state);
  void UpdateTelemetry(const ControlTickInput& input,
                       const ControlTickState& state);

  const ControlLoopContext& ctx_;

  /** State that intentionally survives between 500 Hz ticks. */
  struct PersistentState {
    ControlSetpoint command{};
    ControlSetpoint applied{};
    // Final motor-model input from the previous tick. In Kids this is the
    // counterfactual PWM without speed limiting (LOS-246).
    float motor_model_throttle{0.0f};
    uint32_t last_pwm_update{0};
    bool failsafe_was_active{false};
  };

  VehicleStateEstimator state_estimator_;
  PersistentState persistent_;
  uint32_t diag_loop_count_{0};
  uint32_t diag_start_ms_;

#ifdef RC_PROFILE_LOOP
  // FW-R16: профилирование стадий итерации (debug-сборка). Накапливаем мкс по
  // стадиям и раз в диаг-интервал печатаем средние us/iter. Включается флагом
  // -DRC_PROFILE_LOOP=1; в обычной сборке кода нет (нулевой оверхед).
  void EmitProfile(uint32_t loops);
  // Время снапшота stab-конфига (GetConfig(), включая возможное ожидание
  // config_mutex_ — код-ревью PR #297): отдельная стадия, а не часть comp,
  // иначе ожидание мьютекса (SetConfig() из WS-потока, сама NVS-гипотеза
  // LOS-219) ложно указывало бы на RC/WiFi/IMU как источник задержки.
  uint64_t prof_cfg_us_{0};
  uint64_t prof_components_us_{0};
  uint64_t prof_sensors_us_{0};
  // LOS-219/250: разбивка sens на sensor snapshot и estimator pipeline.
  uint64_t prof_snapshot_us_{0};
  uint64_t prof_ekf_us_{0};
  uint64_t prof_control_us_{0};
  uint64_t prof_stab_us_{0};
  uint64_t prof_pwm_us_{0};
  uint64_t prof_telem_us_{0};
  // Время PrintDiagnostics()/EmitProfile() (сами Log()-вызовы) — код-ревью
  // PR #297: без отдельной стадии эта работа не попадала ни в один
  // per-stage max, хотя занимает реальное время внутри Step() раз в
  // диаг-интервал (см. PROF_LAP-вызов в конце Step()).
  uint64_t prof_diag_us_{0};
  // LOS-219: worst-case per-stage tracking, reset each diag interval same as
  // sums.
  uint64_t prof_cfg_max_us_{0};
  uint64_t prof_components_max_us_{0};
  uint64_t prof_sensors_max_us_{0};
  uint64_t prof_snapshot_max_us_{0};
  uint64_t prof_ekf_max_us_{0};
  uint64_t prof_control_max_us_{0};
  uint64_t prof_stab_max_us_{0};
  uint64_t prof_pwm_max_us_{0};
  uint64_t prof_telem_max_us_{0};
  uint64_t prof_diag_max_us_{0};
  // Max Step()-internal execution time this interval — catches a stall
  // regardless of which stage it lands in (per-stage max alone can't tell
  // you "was this whole iteration slow"). NOT the same as the actual loop
  // period: a stall while the task is blocked in DelayUntilNextTick()
  // (outside Step(), e.g. a flash-cache freeze from a concurrent NVS
  // commit) would leave this unaffected — see prof_period_max_us_/
  // prof_outliers_ below, which use the real entry-to-entry period and are
  // what actually answers "did we miss our target Hz this tick".
  uint64_t prof_step_max_us_{0};
  // GetTimeUs() timestamp of this Step() call's entry, captured by
  // PROF_START() — persists across calls so the NEXT call can compute the
  // real entry-to-entry period at microsecond resolution. 0 sentinel means
  // "not yet set" (first call). NOT derived from dt_ms (code review PR
  // #297): dt_ms comes from GetTimeMs() (whole milliseconds), so
  // dt_ms*1000 can't recover precision already lost to ms-quantization —
  // a period just over the 4ms outlier threshold could round down to
  // dt_ms==4 and silently miss detection.
  uint64_t prof_prev_entry_us_{0};
  // Max real loop period (entry-to-entry, see prof_prev_entry_us_ above) —
  // outlier count is based on THIS, not prof_step_max_us_, per code review
  // on PR #297: basing it on Step()-internal time alone would miss stalls
  // that happen while the task is outside Step() (e.g. blocked in
  // DelayUntilNextTick() during a flash-cache freeze), which is exactly the
  // scenario LOS-219's NVS-commit hypothesis predicts.
  uint64_t prof_period_max_us_{0};
  uint32_t prof_outliers_{0};
#endif
};

}  // namespace rc_vehicle
