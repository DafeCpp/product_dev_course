#pragma once

#include <cstddef>
#include <vector>

#include "com_offset_calibration.hpp"
#include "self_test.hpp"
#include "speed_calibration.hpp"
#include "stabilization_config.hpp"
#include "steering_trim_calibration.hpp"
#include "telemetry_log.hpp"
#include "test_runner.hpp"

namespace rc_vehicle {

/**
 * @brief Интерфейс управления машиной для WS-хендлеров и внешних модулей.
 *
 * Позволяет подставлять mock-реализацию в тестах без зависимости
 * от VehicleControlUnified и всего control loop.
 */
class IVehicleControl {
 public:
  virtual ~IVehicleControl() = default;

  // Команда управления
  virtual void OnWifiCommand(float throttle, float steering) = 0;

  // Калибровка
  virtual void StartCalibration(bool full) = 0;
  virtual bool StartForwardCalibration() = 0;
  virtual bool StartAutoForwardCalibration(float target_accel_g = 0.1f) = 0;
  [[nodiscard]] virtual const char* GetCalibStatus() const = 0;
  [[nodiscard]] virtual int GetCalibStage() const = 0;
  virtual void SetForwardDirection(float fx, float fy, float fz) = 0;

  // Конфигурация стабилизации
  [[nodiscard]] virtual StabilizationConfig GetStabilizationConfig() const = 0;
  virtual bool SetStabilizationConfig(const StabilizationConfig& config,
                                      bool save_to_nvs = true) = 0;

  // Kids mode
  virtual void SetKidsModeActive(bool active) = 0;
  [[nodiscard]] virtual bool IsKidsModeActive() const = 0;

  // Калибровка trim руля
  virtual bool StartSteeringTrimCalibration(float target_accel_g = 0.1f) = 0;
  virtual void StopSteeringTrimCalibration() = 0;
  [[nodiscard]] virtual bool IsSteeringTrimCalibActive() const = 0;
  [[nodiscard]] virtual SteeringTrimCalibration::Result
  GetSteeringTrimCalibResult() const = 0;

  // Калибровка CoM offset
  virtual bool StartComOffsetCalibration(float target_accel_g = 0.1f,
                                         float steering_magnitude = 0.5f,
                                         float cruise_duration_sec = 5.0f) = 0;
  virtual void StopComOffsetCalibration() = 0;
  [[nodiscard]] virtual bool IsComOffsetCalibActive() const = 0;
  [[nodiscard]] virtual ComOffsetCalibration::Result
  GetComOffsetCalibResult() const = 0;

  // Тестовые манёвры
  virtual bool StartTest(const TestParams& params) = 0;
  virtual void StopTest() = 0;
  [[nodiscard]] virtual bool IsTestActive() const = 0;
  [[nodiscard]] virtual TestRunner::Status GetTestStatus() const = 0;

  // Калибровка скорости (throttle → speed gain)
  virtual bool StartSpeedCalibration(float target_throttle = 0.3f,
                                     float cruise_duration_sec = 3.0f) = 0;
  virtual void StopSpeedCalibration() = 0;
  [[nodiscard]] virtual bool IsSpeedCalibActive() const = 0;
  [[nodiscard]] virtual SpeedCalibration::Result
  GetSpeedCalibResult() const = 0;

  // Калибровка магнитометра
  virtual void StartMagCalibration() = 0;
  virtual void FinishMagCalibration() = 0;
  virtual void CancelMagCalibration() = 0;
  [[nodiscard]] virtual const char* GetMagCalibStatus() const = 0;
  virtual bool EraseMagCalibration() = 0;

  // Телеметрия лог
  virtual void GetLogInfo(size_t& count_out, size_t& cap_out) const = 0;
  virtual bool GetLogFrame(size_t idx, TelemetryLogFrame& out) const = 0;
  virtual void ClearLog() = 0;

  // Диагностика
  [[nodiscard]] virtual std::vector<SelfTestItem> RunSelfTest() const = 0;
  [[nodiscard]] virtual bool IsReady() const noexcept = 0;
};

}  // namespace rc_vehicle
