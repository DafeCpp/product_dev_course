#pragma once

#include "esp_err.h"
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
 * Запустить канал WS-телеметрии (низкоприоритетная задача строит JSON из
 * TelemetrySnapshot и шлёт его подключенным клиентам). Вызывать один раз,
 * после firmware_common::esp32::WebSocketRegisterUri().
 */
esp_err_t RcWsTelemStart();

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
  [[nodiscard]] std::expected<void, PlatformError> InitPwm() override;
  [[nodiscard]] std::expected<void, PlatformError> InitRc() override;
  [[nodiscard]] std::expected<void, PlatformError> InitImu() override;
  [[nodiscard]] std::expected<void, PlatformError> InitFailsafe() override;

  // Время
  [[nodiscard]] uint32_t GetTimeMs() const noexcept override;
  [[nodiscard]] uint64_t GetTimeUs() const noexcept override;

  // Логирование
  void Log(LogLevel level, std::string_view msg) const override;
  void LogCoreLoad() const override;

  // IMU
  [[nodiscard]] std::optional<ImuData> ReadImu() override;
  [[nodiscard]] int GetImuLastWhoAmI() const noexcept override;

  // Магнитометр
  bool InitMag() override;
  [[nodiscard]] std::optional<MagData> ReadMag() override;
  [[nodiscard]] const char* GetMagSensorName() const noexcept override;

  // Калибровка магнитометра
  bool SaveMagCalib(const MagCalibData& data) override;
  bool LoadMagCalib(MagCalibData& data) override;
  bool EraseMagCalib() override;

  // Калибровка
  [[nodiscard]] std::optional<ImuCalibData> LoadCalib() override;
  [[nodiscard]] std::expected<void, PlatformError> SaveCalib(
      const ImuCalibData& data) override;

  // CoM offset
  [[nodiscard]] std::expected<void, PlatformError> SaveComOffset(
      const float offset[2]) override;
  [[nodiscard]] bool LoadComOffset(float offset[2]) override;

  // Stabilization Config
  [[nodiscard]] std::optional<StabilizationConfig> LoadStabilizationConfig()
      override;
  [[nodiscard]] std::optional<StabilizationConfig> LoadStabilizationConfig(
      DriveMode mode) override;
  [[nodiscard]] std::expected<void, PlatformError> SaveStabilizationConfig(
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
  void PublishTelem(const TelemetrySnapshot& snap) override;

  // Диагностика
  void PublishDiagnostics(const DiagnosticsSnapshot& snap) override;

  // Wi-Fi команды
  [[nodiscard]] std::optional<RcCommand> TryReceiveWifiCommand() override;
  void SendWifiCommand(float throttle, float steering) override;

  // Задачи
  [[nodiscard]] std::expected<void, PlatformError> CreateTask(
      void (*entry)(void*), void* arg) override;
  void DelayUntilNextTick(uint32_t period_ms) override;

  // Watchdog
  void RegisterTaskWdt() override;
  void FeedTaskWdt() noexcept override;

 private:
  QueueHandle_t cmd_queue_{nullptr};
  Failsafe failsafe_;
  TickType_t last_wake_time_{0};
  bool wake_time_initialized_{false};
};

}  // namespace rc_vehicle
