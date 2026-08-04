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

/**
 * Построить WebSocket-снимок телеметрии.
 *
 * forward_accel_g — продольное линейное ускорение [g] со снятой по текущему
 * тангажу гравитацией (LOS-245), считается в ControlLoopProcessor. Передаётся
 * аргументом, а не вычисляется здесь через ImuCalibration::GetForwardAccel():
 * снимок обязан показывать ровно ту величину, по которой принимает решение
 * accel-лимитер Kids Mode, иначе телеметрия расходится с поведением машины.
 *
 * ВНИМАНИЕ, СМЕНА СЕМАНТИКИ: до LOS-245 snap.forward_accel содержал проекцию
 * с вычетом КОНСТАНТНОЙ вертикали, то есть на наклоне включал sin(pitch)·g.
 * Записи WS-телеметрии до и после несопоставимы. В TelemetryLogFrame этого
 * поля нет, поэтому CSV-логи и analyze_telemetry.py не затронуты — там
 * величина восстанавливается из ax и pitch_deg.
 */
TelemetrySnapshot BuildTelemetrySnapshot(
    const TelemetryContext& ctx, uint32_t now, const SensorSnapshot& sensors,
    const StabilizationConfig& stab_cfg, DriveMode drive_mode,
    float applied_throttle, float applied_steering, float commanded_throttle,
    float commanded_steering, float forward_accel_g);

/**
 * Построить кадр для кольцевого буфера телеметрии.
 *
 * kids_limiters_enabled — kids_mode.limiters_enabled из конфига; сюда он
 * передаётся аргументом, потому что StabilizationConfig в билдер кадра не
 * приходит, а бит kKidsLimitersEnabled нужен для разбора логов (LOS-286:
 * лимитеры Kids живут независимо от stabilization.enabled).
 */
TelemetryLogFrame BuildLogFrame(const TelemetryContext& ctx, uint32_t now,
                                const SensorSnapshot& sensors,
                                float applied_throttle, float applied_steering,
                                float commanded_throttle,
                                float commanded_steering, DriveMode drive_mode,
                                bool stab_enabled, bool kids_limiters_enabled);

}  // namespace rc_vehicle
