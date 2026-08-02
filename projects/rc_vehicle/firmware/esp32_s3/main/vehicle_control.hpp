#pragma once

/**
 * @file vehicle_control.hpp
 * @brief ESP32-совместимый API управления машиной
 *
 * Минимальная обёртка: создание экземпляра VehicleControlUnified и доступ
 * через IVehicleControl&. Все WS-хендлеры получают ссылку через DI
 * (см. ws_command_registry.hpp), а не через глобальные обёртки.
 */

#include "esp_err.h"
#include "vehicle_control_platform_esp32.hpp"
#include "vehicle_control_unified.hpp"

namespace detail {

/** Singleton экземпляр VehicleControlUnified (ESP32-слой владеет lifetime). */
inline rc_vehicle::VehicleControlUnified& GetVehicleControlImpl() {
  static rc_vehicle::VehicleControlUnified instance;
  return instance;
}

/** Доступ через интерфейс IVehicleControl для DI. */
inline rc_vehicle::IVehicleControl& GetVehicleControl() {
  return GetVehicleControlImpl();
}

}  // namespace detail

/** Инициализация платформы и запуск control loop. */
inline esp_err_t VehicleControlInit(void) {
  auto& vc = detail::GetVehicleControlImpl();
  static bool platform_set = false;
  if (!platform_set) {
    auto platform = std::make_unique<rc_vehicle::VehicleControlPlatformEsp32>();
    vc.SetPlatform(std::move(platform));
    platform_set = true;
  }
  auto result = vc.Init();
  return (result == rc_vehicle::PlatformError::Ok) ? ESP_OK : ESP_FAIL;
}

/** Передать Wi-Fi команду в control loop. */
inline void VehicleControlOnWifiCommand(float throttle, float steering) {
  detail::GetVehicleControl().OnWifiCommand(throttle, steering);
}

/** Текущее число кадров в буфере телеметрии и его ёмкость. */
inline void VehicleControlGetLogInfo(size_t* count_out, size_t* cap_out) {
  if (!count_out || !cap_out) {
    return;
  }
  detail::GetVehicleControl().GetLogInfo(*count_out, *cap_out);
}

/** Кадр телеметрии по индексу (0 = самый старый). */
inline bool VehicleControlGetLogFrame(size_t idx, TelemetryLogFrame* out) {
  if (!out) {
    return false;
  }
  return detail::GetVehicleControl().GetLogFrame(idx, *out);
}

inline bool VehicleControlBeginLogExport(size_t* count_out) {
  return count_out && detail::GetVehicleControl().BeginLogExport(*count_out);
}
inline bool VehicleControlBeginLogAndConfigExport(size_t* frame_count_out,
                                                  TelemetryLogFrame* tail_out,
                                                  size_t* snapshot_count_out) {
  return frame_count_out && tail_out && snapshot_count_out &&
         detail::GetVehicleControl().BeginLogAndConfigExport(
             *frame_count_out, *tail_out, *snapshot_count_out);
}
inline bool VehicleControlFinalizeConfigSnapshotExport(
    size_t* snapshot_count_out) {
  return snapshot_count_out &&
         detail::GetVehicleControl().FinalizeConfigSnapshotExport(
             *snapshot_count_out);
}
inline size_t VehicleControlCopyLogExportFrames(size_t start_idx,
                                                TelemetryLogFrame* out,
                                                size_t max_count) {
  return detail::GetVehicleControl().CopyLogExportFrames(start_idx, out,
                                                         max_count);
}
inline void VehicleControlEndLogExport() {
  detail::GetVehicleControl().EndLogExport();
}

/** Количество событий в логе событий (старт/стоп режимов и калибровок). */
inline size_t VehicleControlGetEventCount() {
  return detail::GetVehicleControl().GetEventCount();
}

/** Событие по индексу (0 = самое старое). */
inline bool VehicleControlGetEvent(size_t idx,
                                   rc_vehicle::TelemetryEvent* out) {
  if (!out) {
    return false;
  }
  return detail::GetVehicleControl().GetEvent(idx, *out);
}

inline size_t VehicleControlGetConfigSnapshotCount() {
  return detail::GetVehicleControl().GetConfigSnapshotCount();
}

inline bool VehicleControlGetConfigSnapshot(
    size_t idx, rc_vehicle::TelemetryConfigSnapshot* out) {
  return out && detail::GetVehicleControl().GetConfigSnapshot(idx, *out);
}

inline size_t VehicleControlCopyConfigSnapshots(
    rc_vehicle::TelemetryConfigSnapshot* out, size_t max_count) {
  return detail::GetVehicleControl().CopyConfigSnapshots(out, max_count);
}

inline bool VehicleControlBeginConfigSnapshotExport(uint32_t max_ts_ms,
                                                    size_t* count_out) {
  return count_out && detail::GetVehicleControl().BeginConfigSnapshotExport(
                          max_ts_ms, *count_out);
}

inline bool VehicleControlGetNextConfigSnapshotExport(
    rc_vehicle::TelemetryConfigSnapshot* out) {
  return out && detail::GetVehicleControl().GetNextConfigSnapshotExport(*out);
}

inline void VehicleControlEndConfigSnapshotExport() {
  detail::GetVehicleControl().EndConfigSnapshotExport();
}
