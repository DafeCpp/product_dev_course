#pragma once

#include "failsafe.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "vehicle_control_platform.hpp"

namespace rc_vehicle {

/**
 * @brief Внутренняя структура для передачи Wi-Fi команд через очередь FreeRTOS
 */
struct WifiCmd {
  float throttle;
  float steering;
};

/**
 * @brief Реализация VehicleControlPlatform для ESP32-S3
 *
 * Использует ESP-IDF API для доступа к аппаратным ресурсам:
 * - PWM через LEDC
 * - RC input через GPIO + RMT
 * - IMU через SPI
 * - NVS для хранения калибровки
 * - FreeRTOS для задач и синхронизации
 * - WebSocket для телеметрии и команд
 */
class VehicleControlPlatformEsp32 : public VehicleControlPlatform {
 public:
  VehicleControlPlatformEsp32();
  ~VehicleControlPlatformEsp32() override;

  // Инициализация
  [[nodiscard]] PlatformError InitPwm() override;
  [[nodiscard]] PlatformError InitRc() override;
  [[nodiscard]] PlatformError InitImu() override;
  [[nodiscard]] PlatformError InitFailsafe() override;

  // Время
  [[nodiscard]] uint32_t GetTimeMs() const noexcept override;
  [[nodiscard]] uint64_t GetTimeUs() const noexcept override;

  // Логирование
  void Log(LogLevel level, std::string_view msg) const override;

  // IMU
  [[nodiscard]] std::optional<ImuData> ReadImu() override;
  [[nodiscard]] int GetImuLastWhoAmI() const noexcept override;

  // Калибровка
  [[nodiscard]] std::optional<ImuCalibData> LoadCalib() override;
  [[nodiscard]] bool SaveCalib(const ImuCalibData& data) override;

  // Stabilization Config
  [[nodiscard]] std::optional<StabilizationConfig> LoadStabilizationConfig()
      override;
  [[nodiscard]] bool SaveStabilizationConfig(
      const StabilizationConfig& config) override;

  // RC Input
  [[nodiscard]] std::optional<RcCommand> GetRc() override;

  // PWM Output
  void SetPwm(float throttle, float steering) noexcept override;
  void SetPwmNeutral() noexcept override;

  // Failsafe
  [[nodiscard]] bool FailsafeUpdate(bool rc_active, bool wifi_active) override;
  [[nodiscard]] bool FailsafeIsActive() const noexcept override;

  // WebSocket
  [[nodiscard]] unsigned GetWebSocketClientCount() const noexcept override;
  void SendTelem(std::string_view json) override;

  // Wi-Fi команды
  [[nodiscard]] std::optional<RcCommand> TryReceiveWifiCommand() override;
  void SendWifiCommand(float throttle, float steering) override;

  // Задачи
  [[nodiscard]] bool CreateTask(void (*entry)(void*), void* arg) override;
  void DelayUntilNextTick(uint32_t period_ms) override;

 private:
  QueueHandle_t cmd_queue_{nullptr};
  Failsafe failsafe_;
};

}  // namespace rc_vehicle
