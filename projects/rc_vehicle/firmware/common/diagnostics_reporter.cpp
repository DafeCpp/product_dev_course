#include "diagnostics_reporter.hpp"

#include <iomanip>

#include "config.hpp"
#include "log_format.hpp"

namespace rc_vehicle {

void MaybePublishDiagnostics(const DiagnosticsContext& ctx,
                             const StabilizationConfig& cfg, uint32_t now_ms,
                             uint32_t& diag_loop_count,
                             uint32_t& diag_start_ms) {
  const uint32_t elapsed = now_ms - diag_start_ms;
  if (elapsed < config::DiagnosticsConfig::kIntervalMs) return;

  const uint32_t loop_hz =
      (elapsed > 0) ? (diag_loop_count * 1000u / elapsed) : 0u;
  ctx.last_loop_hz.store(loop_hz, std::memory_order_relaxed);

  DiagnosticsSnapshot snap{};
  snap.loop_hz = loop_hz;
  snap.stab_enabled = cfg.enabled;
  snap.stab_weight = ctx.stab_mgr.GetStabilizationWeight();
  snap.imu_enabled = ctx.imu_handler && ctx.imu_handler->IsEnabled();
  if (snap.imu_enabled) {
    ctx.madgwick.GetEulerDeg(snap.pitch_deg, snap.roll_deg, snap.yaw_deg);
    snap.filtered_gz = ctx.imu_handler->GetFilteredGyroZ();
    snap.ekf_vx = ctx.ekf.GetVx();
    snap.ekf_vy = ctx.ekf.GetVy();
    snap.ekf_slip_deg = ctx.ekf.GetSlipAngleDeg();
  }

  ctx.platform.PublishDiagnostics(snap);

  diag_loop_count = 0;
  diag_start_ms = now_ms;
}

void EmitDiagnostics(VehicleControlPlatform& platform,
                     const DiagnosticsSnapshot& snap) {
  {
    LogFormat fmt;
    fmt << "DIAG: loop=" << snap.loop_hz
        << " Hz  stab=" << (snap.stab_enabled ? "ON" : "OFF")
        << " (w=" << std::fixed << std::setprecision(2) << snap.stab_weight
        << ")";
    platform.Log(LogLevel::Info, fmt.str());
  }

  // LOS-219/250: загрузка ядер — не связано с RC_PROFILE_LOOP, no-op на
  // хосте/симуляторе (см. VehicleControlPlatform::LogCoreLoad()).
  platform.LogCoreLoad();

  if (snap.imu_enabled) {
    {
      LogFormat fmt;
      fmt << "IMU: P=" << std::fixed << std::setprecision(1) << snap.pitch_deg
          << " R=" << snap.roll_deg << " Y=" << snap.yaw_deg
          << " deg  gz=" << snap.filtered_gz << " dps";
      platform.Log(LogLevel::Info, fmt.str());
    }

    {
      LogFormat fmt;
      fmt << "EKF: vx=" << std::fixed << std::setprecision(2) << snap.ekf_vx
          << " vy=" << snap.ekf_vy << " m/s  slip=" << std::setprecision(1)
          << snap.ekf_slip_deg << " deg";
      platform.Log(LogLevel::Info, fmt.str());
    }
  }
}

}  // namespace rc_vehicle
