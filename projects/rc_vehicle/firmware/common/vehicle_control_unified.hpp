#pragma once

#include <atomic>
#include <memory>

#include "control_components.hpp"
#include "imu_calibration.hpp"
#include "madgwick_filter.hpp"
#include "pid_controller.hpp"
#include "stabilization_config.hpp"
#include "vehicle_control_platform.hpp"

namespace rc_vehicle {

/**
 * @brief Унифицированное управление машиной (платформонезависимое)
 *
 * Использует:
 * - VehicleControlPlatform для HAL (PWM, RC, IMU, NVS, WebSocket)
 * - Control Components для модульной обработки (RC, Wi-Fi, IMU, телеметрия)
 * - Failsafe для защиты от потери управления
 *
 * Singleton, control loop работает в отдельной задаче.
 *
 * @note Эта версия заменяет старую common/vehicle_control.hpp и
 *       esp32_s3/main/vehicle_control.hpp, объединяя их функциональность.
 */
class VehicleControlUnified {
 public:
  /** Единственный экземпляр */
  static VehicleControlUnified& Instance();

  /**
   * @brief Установить платформу (должно быть вызвано до Init)
   * @param platform Уникальный указатель на платформу
   */
  void SetPlatform(std::unique_ptr<VehicleControlPlatform> platform);

  /**
   * @brief Инициализация (PWM, RC, IMU, NVS, запуск control loop)
   * @return PlatformError::Ok при успехе
   */
  [[nodiscard]] PlatformError Init();

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
  [[nodiscard]] const char* GetCalibStatus() const;

  /**
   * @brief Текущий этап калибровки
   * @return 0, 1 (стояние), 2 (вперёд/назад)
   */
  [[nodiscard]] int GetCalibStage() const;

  /**
   * @brief Задать направление «вперёд» единичным вектором в СК датчика
   * @param fx X компонента вектора
   * @param fy Y компонента вектора
   * @param fz Z компонента вектора
   */
  void SetForwardDirection(float fx, float fy, float fz);

  /**
   * @brief Получить текущую конфигурацию стабилизации
   * @return Конфигурация стабилизации
   */
  [[nodiscard]] const StabilizationConfig& GetStabilizationConfig() const {
    return stab_config_;
  }

  /**
   * @brief Установить конфигурацию стабилизации
   * @param config Новая конфигурация
   * @param save_to_nvs Сохранить в NVS (по умолчанию true)
   * @return true при успехе
   */
  bool SetStabilizationConfig(const StabilizationConfig& config,
                              bool save_to_nvs = true);

  VehicleControlUnified(const VehicleControlUnified&) = delete;
  VehicleControlUnified& operator=(const VehicleControlUnified&) = delete;

 private:
  VehicleControlUnified() = default;
  ~VehicleControlUnified() = default;

  /**
   * @brief Точка входа для control loop задачи
   * @param arg Указатель на экземпляр VehicleControlUnified
   */
  static void ControlTaskEntry(void* arg);

  /**
   * @brief Основной цикл управления
   */
  void ControlTaskLoop();

  /**
   * @brief Инициализация компонентов control loop
   * @return true при успехе
   */
  bool InitializeComponents();

  /**
   * @brief Обработка запроса калибровки
   * @param now_ms Текущее время
   */
  void ProcessCalibrationRequest(uint32_t now_ms);

  /**
   * @brief Обработка завершения калибровки
   */
  void ProcessCalibrationCompletion();

  /**
   * @brief Выбор источника управления (RC приоритетнее Wi-Fi)
   * @param commanded_throttle Выходной параметр: газ
   * @param commanded_steering Выходной параметр: руль
   * @return true если есть активный источник управления
   */
  bool SelectControlSource(float& commanded_throttle,
                           float& commanded_steering);

  /**
   * @brief Обновление PWM с slew rate
   * @param now_ms Текущее время
   * @param commanded_throttle Целевой газ
   * @param commanded_steering Целевой руль
   * @param applied_throttle Текущий применённый газ (in/out)
   * @param applied_steering Текущий применённый руль (in/out)
   * @param last_pwm_update Время последнего обновления PWM (in/out)
   */
  void UpdatePwmWithSlewRate(uint32_t now_ms, float commanded_throttle,
                             float commanded_steering, float& applied_throttle,
                             float& applied_steering,
                             uint32_t& last_pwm_update);

  /**
   * @brief Вывод диагностической информации
   * @param now_ms Текущее время
   * @param diag_loop_count Счётчик итераций (in/out)
   * @param diag_start_ms Время начала диагностики (in/out)
   */
  void PrintDiagnostics(uint32_t now_ms, uint32_t& diag_loop_count,
                        uint32_t& diag_start_ms);

  // ─────────────────────────────────────────────────────────────────────────
  // Члены класса
  // ─────────────────────────────────────────────────────────────────────────

  // Платформа (HAL)
  std::unique_ptr<VehicleControlPlatform> platform_;

  // Калибровка, фильтр и конфигурация стабилизации
  ImuCalibration imu_calib_;
  MadgwickFilter madgwick_;
  StabilizationConfig stab_config_;

  // ПИД-регулятор yaw rate
  PidController yaw_pid_;

  // Control components
  std::unique_ptr<RcInputHandler> rc_handler_;
  std::unique_ptr<WifiCommandHandler> wifi_handler_;
  std::unique_ptr<ImuHandler> imu_handler_;
  std::unique_ptr<TelemetryHandler> telem_handler_;

  // Флаги состояния
  bool rc_enabled_{false};
  bool imu_enabled_{false};
  bool inited_{false};

  // Плавное включение/выключение стабилизации
  float stab_weight_{0.0f};  // Текущий вес [0..1]: 0 = выкл, 1 = полностью вкл

  // Запрос калибровки (атомарный для потокобезопасности)
  std::atomic<int> calib_request_{0};
};

}  // namespace rc_vehicle