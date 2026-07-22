#include "control_loop_processor.hpp"

#include <cmath>

#include "config.hpp"
#include "diagnostics_reporter.hpp"
#include "drive_mode_registry.hpp"
#include "telemetry_builder.hpp"

#ifdef ESP_PLATFORM
#include "udp_telem_sender.hpp"
#endif

#ifdef RC_PROFILE_LOOP
#include "log_format.hpp"
// FW-R16: засечки времени по стадиям итерации. PROF_START() заводит локальный
// курсор времени, PROF_LAP(acc) добавляет дельту с прошлой засечки в
// аккумулятор и сдвигает курсор. В обычной сборке — пустышки (нулевой оверхед,
// без _pt).
#define PROF_START() uint64_t _pt = ctx_.platform.GetTimeUs()
#define PROF_LAP(acc)                              \
  do {                                             \
    const uint64_t _n = ctx_.platform.GetTimeUs(); \
    (acc) += _n - _pt;                             \
    _pt = _n;                                      \
  } while (0)
#else
#define PROF_START() ((void)0)
#define PROF_LAP(acc) ((void)0)
#endif

namespace rc_vehicle {

void ControlLoopProcessor::Step(uint32_t now, uint32_t dt_ms) {
  ++diag_loop_count_;

  // Единственный snapshot конфига на итерацию (FW-RF5): одна копия под
  // мьютексом вместо трёх (Step/UpdateWeights/диагностика) на 500 Гц.
  stab_cfg_ = ctx_.stab_mgr ? ctx_.stab_mgr->GetConfig() : StabilizationConfig{};

  PROF_START();

  UpdateComponents(now, dt_ms);  // RC/WiFi/IMU read + Madgwick + LPF
  PROF_LAP(prof_components_us_);
  UpdateSensorsAndEkf(dt_ms);  // snapshot + ComOffset + EKF
  PROF_LAP(prof_sensors_us_);

  if (ctx_.calib_mgr) {
    ctx_.calib_mgr->ProcessRequest(now);
    ctx_.calib_mgr->ProcessCompletion(now);
  }

  SelectControlSource(sensors_, commanded_throttle_, commanded_steering_);
  UpdateAutoDrive(now, dt_ms);
  PROF_LAP(prof_control_us_);

  UpdateStabilization(dt_ms);
  PROF_LAP(prof_stab_us_);
  // При активном failsafe UpdatePwm пропускается: иначе SetPwm(0 + trim)
  // перезаписал бы нейтраль ненулевым trim'ом — моторы ползли бы при
  // потере сигнала (FW-R1).
  if (!HandleFailsafe()) {
    UpdatePwm(now, dt_ms);
  }
  PROF_LAP(prof_pwm_us_);
  UpdateTelemetry(now, dt_ms);
  PROF_LAP(prof_telem_us_);

  {
    const DiagnosticsContext dctx{ctx_.platform, *ctx_.stab_mgr, ctx_.madgwick,
                                  ctx_.ekf, ctx_.imu_handler,
                                  ctx_.last_loop_hz};
#ifdef RC_PROFILE_LOOP
    const uint32_t prof_loops = diag_loop_count_;
#endif
    PrintDiagnostics(dctx, stab_cfg_, now, diag_loop_count_, diag_start_ms_);
#ifdef RC_PROFILE_LOOP
    // diag_loop_count_ обнуляется в PrintDiagnostics, когда сработал интервал —
    // это и есть сигнал напечатать средние и сбросить аккумуляторы.
    if (diag_loop_count_ == 0) EmitProfile(prof_loops);
#endif
  }
}

void ControlLoopProcessor::UpdateComponents(uint32_t now, uint32_t dt_ms) {
  if (ctx_.rc_handler) ctx_.rc_handler->Update(now, dt_ms);
  if (ctx_.wifi_handler) ctx_.wifi_handler->Update(now, dt_ms);
  if (ctx_.imu_handler) ctx_.imu_handler->Update(now, dt_ms);
}

void ControlLoopProcessor::UpdateSensorsAndEkf(uint32_t dt_ms) {
  sensors_ = BuildSensorSnapshot(ctx_.rc_handler, ctx_.wifi_handler,
                                 ctx_.imu_handler);
  prev_gz_rad_s_ =
      CorrectImuForComOffset(sensors_, ctx_.imu_calib, prev_gz_rad_s_, dt_ms);

  const bool ekf_active = ctx_.stab_mgr && stab_cfg_.filter.ekf_enabled;
  if (ekf_active && sensors_.imu_enabled && dt_ms > 0) {
    const float dt_sec = static_cast<float>(dt_ms) * 0.001f;
    constexpr float kG = 9.80665f;

    // Ориентация для снятия проекции гравитации из ускорения перед
    // интеграцией в EKF. Источник по умолчанию — TiltEstimator (LOS-240):
    // комплементарный фильтр, не загрязняемый линейным ускорением (в
    // отличие от Madgwick — см. tilt_estimator.hpp). a_lin_prev_g_ хранит
    // оценку продольного линейного ускорения ПРЕДЫДУЩЕГО тика (конечная
    // разность заякоренного EKF vx), что исключает циркулярность внутри
    // одного тика между тангажом и vx, вычисляемым этим же UpdateFromImu.
    // При выключенном tilt-фильтре — фолбэк на Madgwick (обратная
    // совместимость); при выключенных обоих — 0 (без grav-компенсации).
    float pitch_rad = 0.0f, roll_rad = 0.0f;
    if (stab_cfg_.filter.tilt_comp_enabled) {
      tilt_est_.SetParams({stab_cfg_.filter.tilt_corr_gain_hz,
                           stab_cfg_.filter.tilt_accel_gate_band_g});
      // Ротация в СК машины (LOS-240, код-ревью PR #290): при наклонном
      // монтаже IMU bias-corrected ax/ay/gx/gy остаются в осях датчика.
      // Madgwick корректирует это на выходе через SetVehicleFrame(); тут —
      // на входе, поскольку TiltEstimator сам не работает с кватернионами.
      ImuData veh_imu = sensors_.imu_data;
      ctx_.imu_calib.RotateToVehicleFrame(veh_imu);
      tilt_est_.Update(veh_imu, a_lin_prev_g_, dt_sec);
      pitch_rad = tilt_est_.GetPitchRad();
      roll_rad = tilt_est_.GetRollRad();
    } else if (stab_cfg_.filter.madgwick_enabled) {
      float yaw_rad = 0.0f;
      ctx_.madgwick.GetEulerRad(pitch_rad, roll_rad, yaw_rad);
    }
    // Передаём |commanded_throttle_| для ZUPT gating:
    // если throttle > 2%, ZUPT не применяется (машина пытается ехать).
    ctx_.ekf.UpdateFromImu(sensors_.imu_data.ax, sensors_.imu_data.ay,
                           sensors_.imu_data.az, sensors_.filtered_gz, dt_sec,
                           std::abs(commanded_throttle_), pitch_rad, roll_rad);

    // Якорь продольной скорости через мотор-модель (LOS-233): без датчика
    // колёс единственный способ не дать vx уйти в разнос при интеграции IMU.
    // v ≈ gain·throttle (с мёртвой зоной) подаётся слабым измерением.
    // Во время калибровки скорости якорь отключён: иначе калибровка мерила бы
    // EKF-скорость, заякоренную текущим gain, и подтверждала бы сама себя.
    // Вход модели — applied_throttle_ (значение прошлого тика, после slew и
    // trim): это то, что реально ушло в PWM. Команда при slew-рампе прыгает
    // мгновенно и завышала бы ожидаемую скорость на всё время рампы.
    const auto& f = stab_cfg_.filter;
    if (f.motor_model_enabled && !ctx_.auto_drive.IsSpeedCalibActive()) {
      const float thr = applied_throttle_;
      const float thr_abs = std::abs(thr);
      float v_expected = 0.0f;
      if (thr_abs > f.motor_deadzone && f.motor_deadzone < 1.0f) {
        const float sign = thr < 0.0f ? -1.0f : 1.0f;
        v_expected = sign * f.motor_speed_gain * (thr_abs - f.motor_deadzone) /
                     (1.0f - f.motor_deadzone);
      }
      ctx_.ekf.UpdateSpeed(v_expected, f.speed_meas_noise);
    }

    // Неголономное ограничение vy≈0 (LOS-233): гейтим по yaw rate — при
    // явном заносе/вращении боковая скорость реальна, ограничение ослабляем.
    constexpr float kNhcMaxYawRateRps = 1.0f;  // ~57°/с
    if (f.nhc_enabled && std::abs(ctx_.ekf.GetYawRate()) < kNhcMaxYawRateRps) {
      ctx_.ekf.UpdateNonHolonomic(f.nhc_noise);
    }

    // Обновляем оценку продольного линейного ускорения для TiltEstimator
    // СЛЕДУЮЩЕГО тика: конечная разность заякоренного EKF vx этого тика.
    const float vx_now = ctx_.ekf.GetVx();
    a_lin_prev_g_ = (vx_now - prev_vx_) / dt_sec / kG;
    prev_vx_ = vx_now;
  }
  if (ekf_active && sensors_.imu_enabled && sensors_.mag_enabled) {
    constexpr float kDegToRad = 3.14159265358979f / 180.0f;
    ctx_.ekf.UpdateHeading(sensors_.heading_deg * kDegToRad);
  }
}

void ControlLoopProcessor::UpdateAutoDrive(uint32_t now_ms, uint32_t dt_ms) {
  auto ad_input = BuildAutoDriveInput(sensors_, ctx_.imu_calib, dt_ms, now_ms);
  if (sensors_.imu_enabled) {
    ad_input.speed_ms = ctx_.ekf.GetSpeedMs();
  }
  auto ad_out = ctx_.auto_drive.Update(ad_input);
  if (ad_out.active) {
    commanded_throttle_ = ad_out.throttle;
    commanded_steering_ = ad_out.steering;
  }
  HandleAutoDriveCompletion(ad_out, ctx_.stab_mgr, ctx_.imu_calib,
                            ctx_.platform);
}

void ControlLoopProcessor::UpdateStabilization(uint32_t dt_ms) {
  if (!ctx_.stab_mgr) return;

  ctx_.stab_mgr->UpdateWeights(stab_cfg_, dt_ms);

  const DriveMode drive_mode = stab_cfg_.mode;
  const auto traits = DriveModeRegistry::Get(drive_mode).GetTraits();

  if (traits.apply_input_limits) {
    float kids_fwd_accel = 0.0f;
    if (sensors_.imu_enabled) {
      kids_fwd_accel = ctx_.imu_calib.GetForwardAccel(sensors_.imu_data);
    }
    ctx_.kids_processor.Process(stab_cfg_, commanded_throttle_,
                                commanded_steering_, dt_ms, kids_fwd_accel);
  }

  const float sw = ctx_.stab_mgr->GetStabilizationWeight();
  const float mw = ctx_.stab_mgr->GetModeTransitionWeight();

  if (traits.yaw_rate_active)
    ctx_.yaw_ctrl.Process(stab_cfg_, commanded_steering_, sw, mw, dt_ms,
                          commanded_throttle_ < 0.0f);
  if (traits.pitch_comp_active)
    ctx_.pitch_ctrl.Process(stab_cfg_, commanded_throttle_, sw);
  if (traits.slip_angle_active)
    ctx_.slip_ctrl.Process(stab_cfg_, commanded_throttle_, sw, mw, dt_ms);
  if (traits.oversteer_guard_active)
    ctx_.oversteer_guard.Process(stab_cfg_, commanded_throttle_, dt_ms,
                                 traits.oversteer_reduces_throttle);
}

bool ControlLoopProcessor::HandleFailsafe() {
  if (!ctx_.platform.FailsafeUpdate(sensors_.rc_active, sensors_.wifi_active)) {
    failsafe_was_active_ = false;
    return false;
  }

  commanded_throttle_ = 0.0f;
  commanded_steering_ = 0.0f;
  applied_throttle_ = 0.0f;
  applied_steering_ = 0.0f;

  // Сброс подсистем — однократно на переходе Inactive→Active.
  // Повторять каждые 2 мс бессмысленно (EKF/ПИД и так пусты), а EKF
  // при длительном failsafe может продолжать оценку без помех.
  if (!failsafe_was_active_) {
    failsafe_was_active_ = true;
    ctx_.yaw_ctrl.Reset();
    ctx_.slip_ctrl.Reset();
    ctx_.oversteer_guard.Reset();
    ctx_.kids_processor.Reset();
    ctx_.ekf.Reset();
    if (ctx_.stab_mgr) ctx_.stab_mgr->ResetWeights();
    if (ctx_.telem_mgr) ctx_.telem_mgr->ResetLastLogTime();
    ctx_.auto_drive.StopAll();
  }

  // Нейтраль удерживается каждый тик (defense-in-depth)
  ctx_.platform.SetPwmNeutral();
  return true;
}

void ControlLoopProcessor::UpdatePwm(uint32_t now, uint32_t dt_ms) {
  (void)dt_ms;
  const float steer_trim = stab_cfg_.steering_trim;
  const float thr_trim = stab_cfg_.throttle_trim;

  const DriveMode drive_mode = stab_cfg_.mode;
  const auto traits = DriveModeRegistry::Get(drive_mode).GetTraits();

  if (traits.use_slew_rate) {
    float effective_slew_thr = stab_cfg_.slew_throttle;
    if (stab_cfg_.braking_mode == BrakingMode::Brake &&
        std::abs(commanded_throttle_) < std::abs(applied_throttle_)) {
      effective_slew_thr *= stab_cfg_.brake_slew_multiplier;
    }
    UpdatePwmWithSlewRate(ctx_.platform, now, commanded_throttle_,
                          commanded_steering_, applied_throttle_,
                          applied_steering_, last_pwm_update_, thr_trim,
                          steer_trim, effective_slew_thr,
                          stab_cfg_.slew_steering);
  } else {
    applied_throttle_ = commanded_throttle_ + thr_trim;
    applied_steering_ = commanded_steering_ + steer_trim;
    ctx_.platform.SetPwm(applied_throttle_, applied_steering_);
  }
}

void ControlLoopProcessor::UpdateTelemetry(uint32_t now, uint32_t dt_ms) {
  (void)dt_ms;
  const TelemetryContext tctx{ctx_.ekf,    ctx_.madgwick,   ctx_.imu_calib,
                               ctx_.oversteer_guard, ctx_.kids_processor,
                               ctx_.auto_drive};
  const DriveMode drive_mode = stab_cfg_.mode;

  if (ctx_.telem_handler) {
    auto snap = BuildTelemetrySnapshot(tctx, now, sensors_, stab_cfg_,
                                       drive_mode, applied_throttle_,
                                       applied_steering_, commanded_throttle_,
                                       commanded_steering_);
    // FW-RF8: failsafe в снимок — чтобы JSON строился в задаче телеметрии без
    // обращения к платформе из чужого потока.
    snap.failsafe = ctx_.platform.FailsafeIsActive();
    ctx_.telem_handler->SendTelemetry(now, snap);
  }

  if (sensors_.imu_enabled && ctx_.telem_mgr) {
    const uint32_t last_log = ctx_.telem_mgr->GetLastLogTime();
    if (now - last_log >= config::TelemetryLogConfig::kLogIntervalMs) {
      auto frame = BuildLogFrame(tctx, now, sensors_, applied_throttle_,
                                 applied_steering_, commanded_throttle_,
                                 commanded_steering_);
      ctx_.telem_mgr->Push(frame);
      ctx_.telem_mgr->SetLastLogTime(now);
#ifdef ESP_PLATFORM
      UdpTelemEnqueue(frame);
#endif
    }
  }
}

#ifdef RC_PROFILE_LOOP
void ControlLoopProcessor::EmitProfile(uint32_t loops) {
  if (loops == 0) return;
  LogFormat fmt;
  fmt << "PROF(us/iter): comp=" << (prof_components_us_ / loops)
      << " sens=" << (prof_sensors_us_ / loops)
      << " ctrl=" << (prof_control_us_ / loops)
      << " stab=" << (prof_stab_us_ / loops)
      << " pwm=" << (prof_pwm_us_ / loops)
      << " telem=" << (prof_telem_us_ / loops);
  ctx_.platform.Log(LogLevel::Info, fmt.str());
  prof_components_us_ = 0;
  prof_sensors_us_ = 0;
  prof_control_us_ = 0;
  prof_stab_us_ = 0;
  prof_pwm_us_ = 0;
  prof_telem_us_ = 0;
}
#endif

}  // namespace rc_vehicle
