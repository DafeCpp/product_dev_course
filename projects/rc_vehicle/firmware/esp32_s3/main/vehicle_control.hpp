#pragma once

#include <memory>

#include "esp_err.h"
#include "imu_calibration.hpp"
#include "madgwick_filter.hpp"

// Forward declarations
namespace rc_vehicle {
class VehicleControlPlatform;
class RcInputHandler;
class WifiCommandHandler;
class ImuHandler;
class TelemetryHandler;
}  // namespace rc_vehicle

/**
 * @brief Управление машиной с использованием модульной архитектуры
 *
 * Использует:
 * - VehicleControlPlatform для HAL (PWM, RC, IMU, NVS, WebSocket)
 * - Control Components для модульной обработки (RC, Wi-Fi, IMU, телеметрия)
 * - Failsafe для защиты от потери управления
 *
 * Singleton, control loop работает в отдельной задаче FreeRTOS на Core 1.
 */
class VehicleControl {
 public:
  /** Единственный экземпляр */
  static VehicleControl& Instance();

  /**
   * @brief Инициализация (PWM, RC, IMU, NVS, запуск control loop)
   * @return ESP_OK при успехе
   */
  esp_err_t Init();

  /**
   * @brief Команда по Wi‑Fi (WebSocket)
   * @param throttle Газ [-1..1]
   * @param steering Руль [-1..1]
   */
  void OnWifiCommand(float throttle, float steering);

  /**
   * @brief Запуск калибровки IMU, этап 1
   * @param full true — полная (gyro+accel+g), false — только гироскоп
   */
  void StartCalibration(bool full);

  /**
   * @brief Запуск этапа 2 калибровки (движение вперёд/назад)
   * @return true при успешном запуске
   */
  bool StartForwardCalibration();

  /**
   * @brief Строковый статус калибровки
   * @return "idle", "collecting", "done", "failed"
   */
  const char* GetCalibStatus() const;

  /**
   * @brief Текущий этап калибровки
   * @return 0, 1 (стояние), 2 (вперёд/назад)
   */
  int GetCalibStage() const;

  /**
   * @brief Задать направление «вперёд» единичным вектором в СК датчика
   * @param fx X компонента вектора
   * @param fy Y компонента вектора
   * @param fz Z компонента вектора
   */
  void SetForwardDirection(float fx, float fy, float fz);

  VehicleControl(const VehicleControl&) = delete;
  VehicleControl& operator=(const VehicleControl&) = delete;

 private:
  VehicleControl();
  ~VehicleControl();

  static void ControlTaskEntry(void* arg);
  void ControlTaskLoop();

  // Платформа (HAL)
  std::unique_ptr<rc_vehicle::VehicleControlPlatform> platform_;

  // Калибровка и фильтр (общие для всех компонентов)
  ImuCalibration imu_calib_;
  MadgwickFilter madgwick_;

  // Control components
  std::unique_ptr<rc_vehicle::RcInputHandler> rc_handler_;
  std::unique_ptr<rc_vehicle::WifiCommandHandler> wifi_handler_;
  std::unique_ptr<rc_vehicle::ImuHandler> imu_handler_;
  std::unique_ptr<rc_vehicle::TelemetryHandler> telem_handler_;

  // Флаги состояния
  bool rc_enabled_{false};
  bool imu_enabled_{false};
  bool inited_{false};

  // Запрос калибровки (из WebSocket)
  volatile int calib_request_{0};
};

// ═════════════════════════════════════════════════════════════════════════
// Совместимость с существующим кодом (main, WebSocket)
// ═════════════════════════════════════════════════════════════════════════

inline esp_err_t VehicleControlInit(void) {
  return VehicleControl::Instance().Init();
}

inline void VehicleControlOnWifiCommand(float throttle, float steering) {
  VehicleControl::Instance().OnWifiCommand(throttle, steering);
}

inline void VehicleControlStartCalibration(bool full) {
  VehicleControl::Instance().StartCalibration(full);
}

inline bool VehicleControlStartForwardCalibration(void) {
  return VehicleControl::Instance().StartForwardCalibration();
}

inline const char* VehicleControlGetCalibStatus(void) {
  return VehicleControl::Instance().GetCalibStatus();
}

inline int VehicleControlGetCalibStage(void) {
  return VehicleControl::Instance().GetCalibStage();
}

inline void VehicleControlSetForwardDirection(float fx, float fy, float fz) {
  VehicleControl::Instance().SetForwardDirection(fx, fy, fz);
}
