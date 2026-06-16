#include "control_loop_processor.hpp"

#include <cmath>

#include "config.hpp"
#include "diagnostics_reporter.hpp"
#include "drive_mode_registry.hpp"
#include "telemetry_builder.hpp"

#ifdef ESP_PLATFORM
#include "udp_telem_sender.hpp"
#endif

#ifdef RC_DEBUG_STEER_SRC
#include "log_format.hpp"
#endif

namespace rc_vehicle {

void ControlLoopProcessor::Step(uint32_t now, uint32_t dt_ms) {
  ++diag_loop_count_;

  // Единственный snapshot конфига на итерацию (FW-RF5): одна копия под
  // мьютексом вместо трёх (Step/UpdateWeights/диагностика) на 500 Гц.
  stab_cfg_ = ctx_.stab_mgr ? ctx_.stab_mgr->GetConfig() : StabilizationConfig{};

  UpdateComponents(now, dt_ms);
  UpdateSensorsAndEkf(dt_ms);

  if (ctx_.calib_mgr) {
    ctx_.calib_mgr->ProcessRequest(now);
    ctx_.calib_mgr->ProcessCompletion(now);
  }

#ifdef RC_DEBUG_STEER_SRC
  const float dbg_rc =
      (sensors_.rc_active && sensors_.rc_cmd) ? sensors_.rc_cmd->steering : NAN;
  const float dbg_wifi = (sensors_.wifi_active && sensors_.wifi_cmd)
                             ? sensors_.wifi_cmd->steering
                             : NAN;
#endif

  SelectControlSource(sensors_, commanded_throttle_, commanded_steering_);
#ifdef RC_DEBUG_STEER_SRC
  const float dbg_base = commanded_steering_;
#endif
  UpdateAutoDrive(now, dt_ms);
#ifdef RC_DEBUG_STEER_SRC
  const float dbg_post_auto = commanded_steering_;
#endif

  UpdateStabilization(dt_ms);
#ifdef RC_DEBUG_STEER_SRC
  const float dbg_post_stab = commanded_steering_;
#endif
  // При активном failsafe UpdatePwm пропускается: иначе SetPwm(0 + trim)
  // перезаписал бы нейтраль ненулевым trim'ом — моторы ползли бы при
  // потере сигнала (FW-R1).
  if (!HandleFailsafe()) {
    UpdatePwm(now, dt_ms);
  }
#ifdef RC_DEBUG_STEER_SRC
  RecordSteerSample(dbg_rc, dbg_wifi, dbg_base, dbg_cur_auto_active_,
                    dbg_cur_auto_out_, dbg_post_auto, dbg_post_stab,
                    applied_steering_);
#endif
  UpdateTelemetry(now, dt_ms);

  {
    const DiagnosticsContext dctx{ctx_.platform, *ctx_.stab_mgr, ctx_.madgwick,
                                  ctx_.ekf, ctx_.imu_handler,
                                  ctx_.last_loop_hz};
    PrintDiagnostics(dctx, stab_cfg_, now, diag_loop_count_, diag_start_ms_);
#ifdef RC_DEBUG_STEER_SRC
    // diag_loop_count_ обнуляется в PrintDiagnostics при срабатывании
    // интервала.
    if (diag_loop_count_ == 0) EmitSteerSrc();
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
    // Передаём |commanded_throttle_| для ZUPT gating:
    // если throttle > 2%, ZUPT не применяется (машина пытается ехать).
    ctx_.ekf.UpdateFromImu(sensors_.imu_data.ax, sensors_.imu_data.ay,
                           sensors_.imu_data.az, sensors_.filtered_gz,
                           static_cast<float>(dt_ms) * 0.001f,
                           std::abs(commanded_throttle_));
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
#ifdef RC_DEBUG_STEER_SRC
  dbg_cur_auto_active_ = ad_out.active;
  dbg_cur_auto_out_ = ad_out.steering;
#endif
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
    ctx_.kids_processor.Process(commanded_throttle_, commanded_steering_,
                                dt_ms, kids_fwd_accel);
  }

  const float sw = ctx_.stab_mgr->GetStabilizationWeight();
  const float mw = ctx_.stab_mgr->GetModeTransitionWeight();

  if (traits.yaw_rate_active)
    ctx_.yaw_ctrl.Process(commanded_steering_, sw, mw, dt_ms);
  if (traits.pitch_comp_active)
    ctx_.pitch_ctrl.Process(commanded_throttle_, sw);
  if (traits.slip_angle_active)
    ctx_.slip_ctrl.Process(commanded_throttle_, sw, mw, dt_ms);
  if (traits.oversteer_guard_active)
    ctx_.oversteer_guard.Process(commanded_throttle_, dt_ms,
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

#ifdef RC_DEBUG_STEER_SRC
void ControlLoopProcessor::RecordSteerSample(float rc, float wifi, float base,
                                             bool auto_active, float auto_out,
                                             float post_auto, float post_stab,
                                             float applied) {
  const float mag = std::abs(post_stab);
  if (mag <= dbg_worst_mag_) return;
  dbg_worst_mag_ = mag;
  dbg_rc_ = rc;
  dbg_wifi_ = wifi;
  dbg_base_ = base;
  dbg_auto_active_ = auto_active;
  dbg_auto_out_ = auto_out;
  dbg_post_auto_ = post_auto;
  dbg_post_stab_ = post_stab;
  dbg_applied_ = applied;
}

void ControlLoopProcessor::EmitSteerSrc() {
  if (dbg_worst_mag_ < 0.0f) return;  // не было семплов
  LogFormat fmt;
  fmt << "STEER-SRC worst|cmd|=" << dbg_post_stab_ << " : rc=" << dbg_rc_
      << " wifi=" << dbg_wifi_ << " base=" << dbg_base_
      << " auto(act=" << (dbg_auto_active_ ? 1 : 0) << ",out=" << dbg_auto_out_
      << ") post_auto=" << dbg_post_auto_ << " post_stab=" << dbg_post_stab_
      << " applied=" << dbg_applied_;
  ctx_.platform.Log(LogLevel::Info, fmt.str());
  dbg_worst_mag_ = -1.0f;  // сброс на новый интервал
}
#endif

}  // namespace rc_vehicle
