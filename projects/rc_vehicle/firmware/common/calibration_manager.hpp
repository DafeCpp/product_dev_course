#pragma once

#include <atomic>
#include <memory>

#include "imu_calibration.hpp"
#include "madgwick_filter.hpp"
#include "pid_controller.hpp"
#include "vehicle_control_platform.hpp"

namespace rc_vehicle {

// Forward declaration
class VehicleEkf;

/**
 * @brief Менеджер калибровки IMU
 *
 * Отвечает за:
 * - Запуск и управление процессом калибровки IMU
 * - Сохранение/загрузку калибровочных данных через платформу
 * - Обновление фильтра Madgwick при завершении калибровки
 * - Сброс EKF при завершении калибровки (опционально)
 * - Предоставление статуса калибровки
 *
 * Извлечён из VehicleControlUnified для соблюдения Single Responsibility
 * Principle.
 */
class CalibrationManager {
 public:
  /**
   * @brief Конструктор
   * @param platform Платформа для логирования и NVS
   * @param imu_calib Ссылка на объект калибровки IMU
   * @param madgwick Ссылка на фильтр Madgwick
   * @param ekf Указатель на EKF (опционально, для сброса после калибровки)
   */
  CalibrationManager(VehicleControlPlatform& platform,
                     ImuCalibration& imu_calib, MadgwickFilter& madgwick,
                     VehicleEkf* ekf = nullptr);

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
   * @brief Запуск этапа 2 с автоматическим движением вперёд.
   *
   * Прошивка управляет газом через PID-регулятор по продольному ускорению
   * (forward_accel). Это компенсирует разный заряд батареи — throttle
   * подбирается автоматически для поддержания целевого ускорения.
   *
   * RC-пульт перекрывает авто-движение (безопасность).
   * При срабатывании failsafe авто-движение прерывается.
   *
   * @param target_accel_g Целевое ускорение в g [0.02..0.3], по умолчанию 0.1
   * @return true при успешном запуске
   */
  bool StartAutoForwardCalibration(float target_accel_g = 0.1f);

  /** Прервать авто-движение (вызывается из failsafe). */
  void StopAutoForward();

  /** true пока идёт авто-движение для калибровки. */
  [[nodiscard]] bool IsAutoForwardActive() const {
    return auto_forward_active_;
  }

  /**
   * @brief Шаг авто-движения (state machine: разгон → круиз → торможение).
   *
   * Вызывается из control loop каждый тик. Возвращает throttle.
   *
   * @param current_accel_g Текущее продольное ускорение (g)
   * @param accel_magnitude Модуль полного ускорения (g), для детекции остановки
   * @param gyro_z_dps Фильтрованный gyro Z (dps), для детекции остановки
   * @param dt_sec Шаг времени (с)
   * @return throttle [-0.1..0.5]
   */
  float UpdateAutoForward(float current_accel_g, float accel_magnitude,
                          float gyro_z_dps, float dt_sec);

  /**
   * @brief Задать направление «вперёд» единичным вектором в СК датчика
   * @param fx X компонента вектора
   * @param fy Y компонента вектора
   * @param fz Z компонента вектора
   */
  void SetForwardDirection(float fx, float fy, float fz);

  /**
   * @brief Строковый статус калибровки
   * @return "idle", "collecting", "done", "failed"
   */
  [[nodiscard]] const char* GetStatus() const;

  /**
   * @brief Текущий этап калибровки
   * @return 0, 1 (стояние), 2 (вперёд/назад)
   */
  [[nodiscard]] int GetStage() const;

  /**
   * @brief Обработка запроса калибровки (вызывается из control loop)
   * @param now_ms Текущее время
   */
  void ProcessRequest(uint32_t now_ms);

  /**
   * @brief Обработка завершения калибровки (вызывается из control loop)
   */
  void ProcessCompletion();

  /**
   * @brief Загрузить калибровку из NVS при инициализации
   * @return true если калибровка загружена успешно
   */
  bool LoadFromNvs();

  /**
   * @brief Запустить автокалибровку при старте
   */
  void StartAutoCalibration();

 private:
  VehicleControlPlatform& platform_;
  ImuCalibration& imu_calib_;
  MadgwickFilter& madgwick_;
  VehicleEkf* ekf_;  // Опциональная ссылка на EKF для сброса после калибровки

  // Запрос калибровки (атомарный для потокобезопасности)
  std::atomic<int> calib_request_{0};

  // Предыдущий статус калибровки (для логирования только при переходах)
  CalibStatus prev_calib_status_{CalibStatus::Idle};

  // Авто-движение вперёд для Forward-калибровки (PID по ускорению)
  enum class AutoPhase { Idle, Accelerate, Cruise, Brake };
  bool auto_forward_active_{false};
  AutoPhase auto_phase_{AutoPhase::Idle};
  float target_accel_g_{0.1f};
  float phase_elapsed_sec_{0.0f};
  float cruise_throttle_{0.0f};  // throttle зафиксированный в конце разгона
  PidController accel_pid_;

  static constexpr float kAccelDurationSec = 1.5f;
  static constexpr float kCruiseDurationSec = 1.0f;
  static constexpr float kBrakeTimeoutSec = 3.0f;
  // ZUPT-пороги для детекции остановки
  static constexpr float kStopAccelThresh = 0.05f;  // |a| - 1g| < 0.05g
  static constexpr float kStopGyroThresh = 3.0f;    // |gyro_z| < 3 dps
};

}  // namespace rc_vehicle