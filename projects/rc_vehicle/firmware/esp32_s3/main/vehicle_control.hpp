#pragma once

/**
 * @file vehicle_control.hpp
 * @brief ESP32-совместимый API управления машиной
 *
 * Прямые обёртки над rc_vehicle::VehicleControlUnified::Instance().
 */

#include "esp_err.h"
#include "telemetry_log.hpp"
#include "vehicle_control_platform_esp32.hpp"
#include "vehicle_control_unified.hpp"

inline esp_err_t VehicleControlInit(void) {
  static bool platform_set = false;
  if (!platform_set) {
    auto platform =
        std::make_unique<rc_vehicle::VehicleControlPlatformEsp32>();
    rc_vehicle::VehicleControlUnified::Instance().SetPlatform(
        std::move(platform));
    platform_set = true;
  }
  auto result = rc_vehicle::VehicleControlUnified::Instance().Init();
  return (result == rc_vehicle::PlatformError::Ok) ? ESP_OK : ESP_FAIL;
}

inline void VehicleControlOnWifiCommand(float throttle, float steering) {
  rc_vehicle::VehicleControlUnified::Instance().OnWifiCommand(throttle,
                                                              steering);
}

inline void VehicleControlStartCalibration(bool full) {
  rc_vehicle::VehicleControlUnified::Instance().StartCalibration(full);
}

inline bool VehicleControlStartForwardCalibration(void) {
  return rc_vehicle::VehicleControlUnified::Instance()
      .StartForwardCalibration();
}

inline const char* VehicleControlGetCalibStatus(void) {
  return rc_vehicle::VehicleControlUnified::Instance().GetCalibStatus();
}

inline int VehicleControlGetCalibStage(void) {
  return rc_vehicle::VehicleControlUnified::Instance().GetCalibStage();
}

inline void VehicleControlSetForwardDirection(float fx, float fy, float fz) {
  rc_vehicle::VehicleControlUnified::Instance().SetForwardDirection(fx, fy,
                                                                    fz);
}

inline const StabilizationConfig& VehicleControlGetStabilizationConfig(void) {
  return rc_vehicle::VehicleControlUnified::Instance()
      .GetStabilizationConfig();
}

inline bool VehicleControlSetStabilizationConfig(
    const StabilizationConfig& config, bool save_to_nvs = true) {
  return rc_vehicle::VehicleControlUnified::Instance().SetStabilizationConfig(
      config, save_to_nvs);
}

inline void VehicleControlGetLogInfo(size_t* count_out, size_t* cap_out) {
  size_t cnt = 0, cap = 0;
  rc_vehicle::VehicleControlUnified::Instance().GetLogInfo(cnt, cap);
  if (count_out) *count_out = cnt;
  if (cap_out) *cap_out = cap;
}

inline bool VehicleControlGetLogFrame(size_t idx, TelemetryLogFrame* out) {
  if (!out) return false;
  return rc_vehicle::VehicleControlUnified::Instance().GetLogFrame(idx, *out);
}

inline void VehicleControlClearLog() {
  rc_vehicle::VehicleControlUnified::Instance().ClearLog();
}
