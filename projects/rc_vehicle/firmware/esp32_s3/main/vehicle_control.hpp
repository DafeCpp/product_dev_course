#pragma once

/**
 * @file vehicle_control.hpp
 * @brief Обёртка над унифицированной версией VehicleControl
 *
 * Этот файл предоставляет совместимый API для ESP32, делегируя
 * все вызовы к VehicleControlUnified из common/.
 */

#include "esp_err.h"
#include "vehicle_control_platform_esp32.hpp"
#include "vehicle_control_unified.hpp"

/**
 * @brief Управление машиной (ESP32 обёртка)
 *
 * Делегирует все вызовы к rc_vehicle::VehicleControlUnified,
 * предоставляя совместимый API с esp_err_t.
 */
class VehicleControl {
 public:
  /** Единственный экземпляр */
  static VehicleControl& Instance() {
    static VehicleControl s_instance;
    return s_instance;
  }

  /**
   * @brief Инициализация (PWM, RC, IMU, NVS, запуск control loop)
   * @return ESP_OK при успехе
   */
  esp_err_t Init() {
    // Создать и установить платформу при первом вызове
    if (!platform_set_) {
      auto platform =
          std::make_unique<rc_vehicle::VehicleControlPlatformEsp32>();
      unified_.SetPlatform(std::move(platform));
      platform_set_ = true;
    }

    auto result = unified_.Init();
    return (result == rc_vehicle::PlatformError::Ok) ? ESP_OK : ESP_FAIL;
  }

  /**
   * @brief Команда по Wi‑Fi (WebSocket)
   * @param throttle Газ [-1..1]
   * @param steering Руль [-1..1]
   */
  void OnWifiCommand(float throttle, float steering) {
    unified_.OnWifiCommand(throttle, steering);
  }

  /**
   * @brief Запуск калибровки IMU, этап 1
   * @param full true — полная (gyro+accel+g), false — только гироскоп
   */
  void StartCalibration(bool full) { unified_.StartCalibration(full); }

  /**
   * @brief Запуск этапа 2 калибровки (движение вперёд/назад)
   * @return true при успешном запуске
   */
  bool StartForwardCalibration() { return unified_.StartForwardCalibration(); }

  /**
   * @brief Строковый статус калибровки
   * @return "idle", "collecting", "done", "failed"
   */
  const char* GetCalibStatus() const { return unified_.GetCalibStatus(); }

  /**
   * @brief Текущий этап калибровки
   * @return 0, 1 (стояние), 2 (вперёд/назад)
   */
  int GetCalibStage() const { return unified_.GetCalibStage(); }

  /**
   * @brief Задать направление «вперёд» единичным вектором в СК датчика
   * @param fx X компонента вектора
   * @param fy Y компонента вектора
   * @param fz Z компонента вектора
   */
  void SetForwardDirection(float fx, float fy, float fz) {
    unified_.SetForwardDirection(fx, fy, fz);
  }

  /**
   * @brief Получить текущую конфигурацию стабилизации
   * @return Конфигурация стабилизации
   */
  const StabilizationConfig& GetStabilizationConfig() const {
    return unified_.GetStabilizationConfig();
  }

  /**
   * @brief Установить конфигурацию стабилизации
   * @param config Новая конфигурация
   * @param save_to_nvs Сохранить в NVS (по умолчанию true)
   * @return true при успехе
   */
  bool SetStabilizationConfig(const StabilizationConfig& config,
                              bool save_to_nvs = true) {
    return unified_.SetStabilizationConfig(config, save_to_nvs);
  }

  VehicleControl(const VehicleControl&) = delete;
  VehicleControl& operator=(const VehicleControl&) = delete;

 private:
  VehicleControl() = default;
  ~VehicleControl() = default;

  rc_vehicle::VehicleControlUnified& unified_ =
      rc_vehicle::VehicleControlUnified::Instance();
  bool platform_set_{false};
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

inline const StabilizationConfig& VehicleControlGetStabilizationConfig(void) {
  return VehicleControl::Instance().GetStabilizationConfig();
}

inline bool VehicleControlSetStabilizationConfig(
    const StabilizationConfig& config, bool save_to_nvs = true) {
  return VehicleControl::Instance().SetStabilizationConfig(config, save_to_nvs);
}
