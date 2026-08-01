#pragma once

#include <atomic>
#include <cstdint>
#include <type_traits>

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
 * LOS-252: POD-снимок диагностики — та же идея, что TelemetrySnapshot
 * (FW-RF8): control loop публикует только дешёвый снимок, а форматирование
 * строк (LogFormat/iostream) и LogCoreLoad() (обход ~16 FreeRTOS-задач через
 * uxTaskGetSystemState()) выполняются вне 500 Гц пути — см. EmitDiagnostics().
 */
struct DiagnosticsSnapshot {
  uint32_t loop_hz{0};
  bool stab_enabled{false};
  float stab_weight{0.0f};
  bool imu_enabled{false};
  float pitch_deg{0.0f};
  float roll_deg{0.0f};
  float yaw_deg{0.0f};
  float filtered_gz{0.0f};
  float ekf_vx{0.0f};
  float ekf_vy{0.0f};
  float ekf_slip_deg{0.0f};
};

static_assert(
    std::is_trivially_copyable_v<DiagnosticsSnapshot>,
    "DiagnosticsSnapshot must stay trivially copyable for queue copy");

/**
 * @brief Раз в config::DiagnosticsConfig::kIntervalMs собирает снимок и
 * публикует его через ctx.platform.PublishDiagnostics(); иначе no-op.
 *
 * Вызывается каждую итерацию loop. Дешёвая часть (интервал-гейт, обновление
 * last_loop_hz, чтение нескольких float-геттеров) остаётся в control loop;
 * форматирование/печать делает платформа (PublishDiagnostics) вне горячего
 * 500 Гц пути — см. EmitDiagnostics().
 *
 * @param cfg Snapshot конфигурации стабилизации текущей итерации (FW-RF5)
 */
void MaybePublishDiagnostics(const DiagnosticsContext& ctx,
                             const StabilizationConfig& cfg, uint32_t now_ms,
                             uint32_t& diag_loop_count,
                             uint32_t& diag_start_ms);

/**
 * @brief Отформатировать и вывести диагностику (loop Hz, IMU, EKF) из
 * готового снимка, плюс LogCoreLoad() (загрузка ядер).
 *
 * Чистая функция от platform+snapshot — вызывается из низкоприоритетной
 * задачи на ESP32 (см. DiagLogTask в vehicle_control_platform_esp32.cpp),
 * синхронно на host/mock.
 */
void EmitDiagnostics(VehicleControlPlatform& platform,
                     const DiagnosticsSnapshot& snap);

}  // namespace rc_vehicle
