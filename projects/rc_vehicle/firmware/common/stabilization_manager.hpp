#pragma once

#include <memory>

#include "control_components.hpp"
#include "madgwick_filter.hpp"
#include "stabilization_config.hpp"
#include "stabilization_pipeline.hpp"
#include "vehicle_control_platform.hpp"

namespace rc_vehicle {

/**
 * @brief Менеджер конфигурации стабилизации
 *
 * Отвечает за:
 * - Управление конфигурацией стабилизации
 * - Валидацию и применение параметров
 * - Сохранение/загрузку конфигурации через платформу
 * - Обновление параметров фильтров и контроллеров
 * - Управление плавным переходом между режимами
 *
 * Извлечён из VehicleControlUnified для соблюдения Single Responsibility
 * Principle.
 */
class StabilizationManager {
 public:
  /**
   * @brief Конструктор
   * @param platform Платформа для логирования и NVS
   * @param madgwick Ссылка на фильтр Madgwick
   * @param yaw_ctrl Ссылка на контроллер yaw rate
   * @param slip_ctrl Ссылка на контроллер slip angle
   * @param imu_handler Указатель на обработчик IMU (может быть nullptr)
   */
  StabilizationManager(VehicleControlPlatform& platform,
                       MadgwickFilter& madgwick, YawRateController& yaw_ctrl,
                       SlipAngleController& slip_ctrl, ImuHandler* imu_handler);

  /**
   * @brief Получить текущую конфигурацию стабилизации
   * @return Конфигурация стабилизации
   */
  [[nodiscard]] const StabilizationConfig& GetConfig() const { return config_; }

  /**
   * @brief Установить конфигурацию стабилизации
   * @param config Новая конфигурация
   * @param save_to_nvs Сохранить в NVS (по умолчанию true)
   * @return true при успехе
   */
  bool SetConfig(const StabilizationConfig& config, bool save_to_nvs = true);

  /**
   * @brief Загрузить конфигурацию из NVS при инициализации
   * @return true если конфигурация загружена успешно
   */
  bool LoadFromNvs();

  /**
   * @brief Применить конфигурацию к фильтрам и контроллерам
   */
  void ApplyConfig();

  /**
   * @brief Получить текущий вес стабилизации [0..1]
   * @return Вес стабилизации (0 = выкл, 1 = полностью вкл)
   */
  [[nodiscard]] float GetStabilizationWeight() const { return stab_weight_; }

  /**
   * @brief Получить текущий вес перехода между режимами [0..1]
   * @return Вес перехода (0 = начало перехода, 1 = переход завершён)
   */
  [[nodiscard]] float GetModeTransitionWeight() const {
    return mode_transition_weight_;
  }

  /**
   * @brief Обновить веса стабилизации и перехода (вызывается из control loop)
   * @param dt_ms Время с последнего обновления
   */
  void UpdateWeights(uint32_t dt_ms);

  /**
   * @brief Сбросить веса при failsafe
   */
  void ResetWeights();

 private:
  VehicleControlPlatform& platform_;
  MadgwickFilter& madgwick_;
  YawRateController& yaw_ctrl_;
  SlipAngleController& slip_ctrl_;
  ImuHandler* imu_handler_;

  StabilizationConfig config_;

  // Плавное включение/выключение стабилизации
  float stab_weight_{0.0f};  // Текущий вес [0..1]: 0 = выкл, 1 = полностью вкл

  // Плавный переход между режимами (mode transition fade)
  // Сбрасывается в 0 при смене режима, нарастает к 1 за fade_ms
  float mode_transition_weight_{1.0f};
};

}  // namespace rc_vehicle