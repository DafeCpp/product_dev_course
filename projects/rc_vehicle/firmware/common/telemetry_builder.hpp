#pragma once

#include "auto_drive_coordinator.hpp"
#include "control_components.hpp"
#include "imu_calibration.hpp"
#include "kids_mode_processor.hpp"
#include "madgwick_filter.hpp"
#include "stabilization_config.hpp"
#include "stabilization_pipeline.hpp"
#include "telemetry_log.hpp"
#include "vehicle_ekf.hpp"

namespace rc_vehicle {

/** Ссылки на подсистемы, нужные для построения телеметрии. */
struct TelemetryContext {
  const VehicleEkf& ekf;
  const MadgwickFilter& madgwick;
  const ImuCalibration& imu_calib;
  const OversteerGuard& oversteer_guard;
  const KidsModeProcessor& kids_processor;
  const AutoDriveCoordinator& auto_drive;
};

/** Построить WebSocket-снимок телеметрии. */
TelemetrySnapshot BuildTelemetrySnapshot(
    const TelemetryContext& ctx, uint32_t now, const SensorSnapshot& sensors,
    const StabilizationConfig& stab_cfg, DriveMode drive_mode,
    float applied_throttle, float applied_steering, float commanded_throttle,
    float commanded_steering);

/** Построить кадр для кольцевого буфера телеметрии. */
TelemetryLogFrame BuildLogFrame(const TelemetryContext& ctx, uint32_t now,
                                const SensorSnapshot& sensors,
                                float applied_throttle, float applied_steering,
                                float commanded_throttle,
                                float commanded_steering);

}  // namespace rc_vehicle
