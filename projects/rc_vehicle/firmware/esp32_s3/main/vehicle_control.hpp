#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "imu_calibration.hpp"
#include "madgwick_filter.hpp"

/**
 * Управление машиной: PWM, RC-in, IMU, калибровка, failsafe, телеметрия.
 * Один экземпляр (singleton), control loop работает в отдельной задаче FreeRTOS
 * на Core 1.
 */
class VehicleControl {
 public:
  /** Единственный экземпляр. */
  static VehicleControl& Instance();

  /** Инициализация (PWM, RC, IMU, NVS, запуск control loop). @return ESP_OK при
   * успехе. */
  esp_err_t Init();

  /** Команда по Wi‑Fi (WebSocket). throttle/steering в [-1..1]. */
  void OnWifiCommand(float throttle, float steering);

  /** Запуск калибровки IMU, этап 1. @param full true — полная (gyro+accel+g),
   * false — только гироскоп. */
  void StartCalibration(bool full);

  /** Запуск этапа 2 калибровки (движение вперёд/назад). Требуется выполненный
   * этап 1. */
  bool StartForwardCalibration();

  /** Строковый статус калибровки: "idle", "collecting", "done", "failed". */
  const char* GetCalibStatus() const;

  /** Текущий этап калибровки: 0, 1 (стояние), 2 (вперёд/назад). */
  int GetCalibStage() const;

  /** Задать направление «вперёд» единичным вектором в СК датчика, сохранить в
   * NVS. */
  void SetForwardDirection(float fx, float fy, float fz);

  VehicleControl(const VehicleControl&) = delete;
  VehicleControl& operator=(const VehicleControl&) = delete;

 private:
  VehicleControl() = default;

  static void ControlTaskEntry(void* arg);
  void ControlTaskLoop();

  QueueHandle_t cmd_queue_{nullptr};
  bool rc_enabled_{false};
  bool imu_enabled_{false};
  bool inited_{false};
  ImuCalibration imu_calib_;
  MadgwickFilter madgwick_;
  volatile int calib_request_{0};
};

// Совместимость с существующим кодом (main, WebSocket)
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
