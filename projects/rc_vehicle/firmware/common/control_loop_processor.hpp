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
  float applied_throttle_{0.0f};
  float applied_steering_{0.0f};
  float prev_gz_rad_s_{0.0f};
  bool failsafe_was_active_{false};
  uint32_t last_pwm_update_;
  uint32_t diag_loop_count_{0};
  uint32_t diag_start_ms_;

  // Кэшированный снимок датчиков (обновляется в UpdateSensorsAndEkf)
  SensorSnapshot sensors_;
  StabilizationConfig stab_cfg_;

#ifdef RC_DEBUG_STEER_SRC
  // FW-R17: отладочная трассировка источника команды руля. Раз в диаг-интервал
  // печатаем стадию с наибольшим |руля| — видно, кто вбрасывает ±1.0 (RC /
  // Wi-Fi / auto-drive / стабилизация / залипшая commanded_steering_).
  // Включается -DRC_DEBUG_STEER_SRC=1; в обычной сборке кода нет.
  void RecordSteerSample(float rc, float wifi, float base, bool auto_active,
                         float auto_out, float post_auto, float post_stab,
                         float applied);
  void EmitSteerSrc();
  // Снимок «худшего» (макс |post_stab|) семпла за интервал.
  float dbg_worst_mag_{-1.0f};
  float dbg_rc_{0.0f};
  float dbg_wifi_{0.0f};
  float dbg_base_{0.0f};
  bool dbg_auto_active_{false};
  float dbg_auto_out_{0.0f};
  float dbg_post_auto_{0.0f};
  float dbg_post_stab_{0.0f};
  float dbg_applied_{0.0f};
  // Заполняется в UpdateAutoDrive, читается в Step.
  bool dbg_cur_auto_active_{false};
  float dbg_cur_auto_out_{0.0f};
#endif
};

}  // namespace rc_vehicle
