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
