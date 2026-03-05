#pragma once

#include <atomic>
#include <memory>

#include "imu_calibration.hpp"
#include "madgwick_filter.hpp"
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
};

}  // namespace rc_vehicle