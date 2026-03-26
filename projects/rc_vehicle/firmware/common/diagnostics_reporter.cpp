#include "diagnostics_reporter.hpp"

#include <iomanip>

#include "config.hpp"
#include "log_format.hpp"

namespace rc_vehicle {

void PrintDiagnostics(const DiagnosticsContext& ctx, uint32_t now_ms,
                      uint32_t& diag_loop_count, uint32_t& diag_start_ms) {
  const uint32_t elapsed = now_ms - diag_start_ms;
  if (elapsed < config::DiagnosticsConfig::kIntervalMs) return;

  const uint32_t loop_hz =
      (elapsed > 0) ? (diag_loop_count * 1000u / elapsed) : 0u;
  ctx.last_loop_hz.store(loop_hz, std::memory_order_relaxed);

  const auto& cfg = ctx.stab_mgr.GetConfig();
  const float stab_weight = ctx.stab_mgr.GetStabilizationWeight();

  {
    LogFormat fmt;
    fmt << "DIAG: loop=" << static_cast<unsigned>(loop_hz)
        << " Hz  stab=" << (cfg.enabled ? "ON" : "OFF")
        << " (w=" << std::fixed << std::setprecision(2) << stab_weight << ")";
    ctx.platform.Log(LogLevel::Info, fmt.str());
  }

  if (ctx.imu_handler && ctx.imu_handler->IsEnabled()) {
    float pitch_deg = 0.f, roll_deg = 0.f, yaw_deg = 0.f;
    ctx.madgwick.GetEulerDeg(pitch_deg, roll_deg, yaw_deg);

    {
      LogFormat fmt;
      fmt << "IMU: P=" << std::fixed << std::setprecision(1) << pitch_deg
          << " R=" << roll_deg << " Y=" << yaw_deg
          << " deg  gz=" << ctx.imu_handler->GetFilteredGyroZ() << " dps";
      ctx.platform.Log(LogLevel::Info, fmt.str());
    }

    {
      LogFormat fmt;
      fmt << "EKF: vx=" << std::fixed << std::setprecision(2)
          << ctx.ekf.GetVx() << " vy=" << ctx.ekf.GetVy()
          << " m/s  slip=" << std::setprecision(1)
          << ctx.ekf.GetSlipAngleDeg() << " deg";
      ctx.platform.Log(LogLevel::Info, fmt.str());
    }
  }

  diag_loop_count = 0;
  diag_start_ms = now_ms;
}

}  // namespace rc_vehicle
