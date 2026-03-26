#include "control_loop_processor.hpp"

#include "config.hpp"
#include "diagnostics_reporter.hpp"
#include "drive_mode_registry.hpp"
#include "telemetry_builder.hpp"

#ifdef ESP_PLATFORM
#include "udp_telem_sender.hpp"
#endif

namespace rc_vehicle {

void ControlLoopProcessor::Step(uint32_t now, uint32_t dt_ms) {
  ++diag_loop_count_;

  UpdateComponents(now, dt_ms);
  UpdateSensorsAndEkf(dt_ms);

  if (ctx_.calib_mgr) {
    ctx_.calib_mgr->ProcessRequest(now);
    ctx_.calib_mgr->ProcessCompletion();
  }

  SelectControlSource(sensors_, commanded_throttle_, commanded_steering_);
  UpdateAutoDrive(dt_ms);

  stab_cfg_ = ctx_.stab_mgr ? ctx_.stab_mgr->GetConfig() : StabilizationConfig{};

  UpdateStabilization(dt_ms);
  HandleFailsafe();
  UpdatePwm(now, dt_ms);
  UpdateTelemetry(now, dt_ms);

  {
    const DiagnosticsContext dctx{ctx_.platform, *ctx_.stab_mgr, ctx_.madgwick,
                                  ctx_.ekf, ctx_.imu_handler,
                                  ctx_.last_loop_hz};
    PrintDiagnostics(dctx, now, diag_loop_count_, diag_start_ms_);
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

  const bool ekf_active =
      ctx_.stab_mgr && ctx_.stab_mgr->GetConfig().filter.ekf_enabled;
  if (ekf_active && sensors_.imu_enabled && dt_ms > 0) {
    ctx_.ekf.UpdateFromImu(sensors_.imu_data.ax, sensors_.imu_data.ay,
                           sensors_.imu_data.az, sensors_.filtered_gz,
                           static_cast<float>(dt_ms) * 0.001f);
  }
}

void ControlLoopProcessor::UpdateAutoDrive(uint32_t dt_ms) {
  auto ad_out = ctx_.auto_drive.Update(
      BuildAutoDriveInput(sensors_, ctx_.imu_calib, dt_ms));
  if (ad_out.active) {
    commanded_throttle_ = ad_out.throttle;
    commanded_steering_ = ad_out.steering;
  }
  HandleAutoDriveCompletion(ad_out, ctx_.stab_mgr, ctx_.imu_calib,
                            ctx_.platform);
}

void ControlLoopProcessor::UpdateStabilization(uint32_t dt_ms) {
  if (!ctx_.stab_mgr) return;

  ctx_.stab_mgr->UpdateWeights(dt_ms);

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

void ControlLoopProcessor::HandleFailsafe() {
  if (!ctx_.platform.FailsafeUpdate(sensors_.rc_active, sensors_.wifi_active))
    return;

  commanded_throttle_ = 0.0f;
  commanded_steering_ = 0.0f;
  applied_throttle_ = 0.0f;
  applied_steering_ = 0.0f;
  ctx_.yaw_ctrl.Reset();
  ctx_.slip_ctrl.Reset();
  ctx_.oversteer_guard.Reset();
  ctx_.kids_processor.Reset();
  ctx_.ekf.Reset();
  if (ctx_.stab_mgr) ctx_.stab_mgr->ResetWeights();
  if (ctx_.telem_mgr) ctx_.telem_mgr->ResetLastLogTime();
  ctx_.auto_drive.StopAll();
  ctx_.platform.SetPwmNeutral();
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
        commanded_throttle_ < applied_throttle_) {
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

}  // namespace rc_vehicle
