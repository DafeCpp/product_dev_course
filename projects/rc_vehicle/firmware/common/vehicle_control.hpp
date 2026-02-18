#pragma once

#include "imu_calibration.hpp"
#include "madgwick_filter.hpp"
#include "vehicle_control_platform.hpp"

/**
 * Управление машиной: PWM, RC-in, IMU, калибровка, failsafe, телеметрия.
 * Общая логика; ввод/вывод идут через VehicleControlPlatform (реализация на целевой платформе).
 * Один экземпляр (singleton), control loop работает в отдельной задаче (создаётся платформой).
 */
class VehicleControl {
 public:
  static VehicleControl& Instance();

  /** Задать платформенный HAL. Вызывать до Init(). */
  void SetPlatform(VehicleControlPlatform* platform) { platform_ = platform; }

  /** Инициализация (через platform_: PWM, RC, IMU, NVS, запуск задачи). 0 — успех. */
  int Init();

  /** Команда по Wi‑Fi (WebSocket). throttle/steering в [-1..1]. */
  void OnWifiCommand(float throttle, float steering);

  void StartCalibration(bool full);
  bool StartForwardCalibration();
  const char* GetCalibStatus() const;
  int GetCalibStage() const;
  void SetForwardDirection(float fx, float fy, float fz);

  VehicleControl(const VehicleControl&) = delete;
  VehicleControl& operator=(const VehicleControl&) = delete;

 private:
  VehicleControl() = default;

  static void ControlTaskEntry(void* arg);
  void ControlTaskLoop();

  VehicleControlPlatform* platform_{nullptr};
  bool rc_enabled_{false};
  bool imu_enabled_{false};
  bool inited_{false};
  ImuCalibration imu_calib_;
  MadgwickFilter madgwick_;
  volatile int calib_request_{0};
};
