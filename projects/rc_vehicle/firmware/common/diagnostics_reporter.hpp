#pragma once

#include <atomic>
#include <cstdint>

#include "control_components.hpp"
#include "madgwick_filter.hpp"
#include "stabilization_manager.hpp"
#include "vehicle_control_platform.hpp"
#include "vehicle_ekf.hpp"

namespace rc_vehicle {

/** Ссылки на подсистемы, нужные для диагностики. */
struct DiagnosticsContext {
  VehicleControlPlatform& platform;
  const StabilizationManager& stab_mgr;
  const MadgwickFilter& madgwick;
  const VehicleEkf& ekf;
  const ImuHandler* imu_handler;
  std::atomic<uint32_t>& last_loop_hz;
};

/**
 * @brief Вывод диагностической информации (частота loop, IMU, EKF).
 *
 * Вызывается каждую итерацию loop. Выводит информацию с заданным
 * интервалом (config::DiagnosticsConfig::kIntervalMs).
 */
void PrintDiagnostics(const DiagnosticsContext& ctx, uint32_t now_ms,
                      uint32_t& diag_loop_count, uint32_t& diag_start_ms);

}  // namespace rc_vehicle
