#pragma once

#include <atomic>
#include <memory>

#include "calibration_manager.hpp"
#include "com_offset_calibration.hpp"
#include "control_components.hpp"
#include "drive_mode_registry.hpp"
#include "i_vehicle_control.hpp"
#include "imu_calibration.hpp"
#include "self_test.hpp"
#include "kids_mode_processor.hpp"
#include "madgwick_filter.hpp"
#include "stabilization_config.hpp"
#include "stabilization_manager.hpp"
#include "stabilization_pipeline.hpp"
#include "steering_trim_calibration.hpp"
#include "telemetry_manager.hpp"
#include "test_runner.hpp"
#include "vehicle_control_platform.hpp"
#include "vehicle_ekf.hpp"

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
class VehicleControlUnified : public IVehicleControl {
 public:
  VehicleControlUnified() = default;
  ~VehicleControlUnified() override = default;

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
  void OnWifiCommand(float throttle, float steering) override;

  /**
   * @brief Запуск калибровки IMU, этап 1
   * @param full true — полная (gyro+accel+g), false — только гироскоп
   */
  void StartCalibration(bool full) override { calib_mgr_->StartCalibration(full); }

  /**
   * @brief Запуск этапа 2 калибровки (движение вперёд/назад)
   * @return true при успешном запуске
   */
  bool StartForwardCalibration() override {
    return calib_mgr_->StartForwardCalibration();
  }

  /**
   * @brief Запуск этапа 2 с автоматическим движением вперёд.
   * @param target_accel_g Целевое ускорение в g [0.02..0.3], по умолчанию 0.1
   * @return true при успешном запуске
   */
  bool StartAutoForwardCalibration(float target_accel_g = 0.1f) override {
    return calib_mgr_->StartAutoForwardCalibration(target_accel_g);
  }

  /**
   * @brief Строковый статус калибровки
   * @return "idle", "collecting", "done", "failed"
   */
  [[nodiscard]] const char* GetCalibStatus() const override {
    return calib_mgr_->GetStatus();
  }

  /**
   * @brief Текущий этап калибровки
   * @return 0, 1 (стояние), 2 (вперёд/назад)
   */
  [[nodiscard]] int GetCalibStage() const override { return calib_mgr_->GetStage(); }

  /**
   * @brief Задать направление «вперёд» единичным вектором в СК датчика
   * @param fx X компонента вектора
   * @param fy Y компонента вектора
   * @param fz Z компонента вектора
   */
  void SetForwardDirection(float fx, float fy, float fz) override {
    calib_mgr_->SetForwardDirection(fx, fy, fz);
  }

  /**
   * @brief Запуск автокалибровки steering trim
   * @param target_accel_g Целевое ускорение при разгоне [0.02..0.3 g]
   * @return true при успешном запуске
   */
  bool StartSteeringTrimCalibration(float target_accel_g = 0.1f) override;

  /** Прервать калибровку trim руля. */
  void StopSteeringTrimCalibration() override { trim_calib_.Stop(); }

  /** true пока идёт калибровка trim. */
  [[nodiscard]] bool IsSteeringTrimCalibActive() const override {
    return trim_calib_.IsActive();
  }

  /** Результат калибровки trim (валиден после завершения). */
  [[nodiscard]] SteeringTrimCalibration::Result
  GetSteeringTrimCalibResult() const override {
    return trim_calib_.GetResult();
  }

  /**
   * @brief Запустить круговую калибровку IMU→CoM
   * @param target_accel_g Целевое ускорение при разгоне
   * @param steering_magnitude Абсолютное значение руля
   * @param cruise_duration_sec Длительность круизной фазы
   * @return true при успешном запуске
   */
  bool StartComOffsetCalibration(float target_accel_g = 0.1f,
                                 float steering_magnitude = 0.5f,
                                 float cruise_duration_sec = 5.0f) override;

  /** Прервать калибровку CoM offset. */
  void StopComOffsetCalibration() override { com_calib_.Stop(); }

  /** true пока идёт калибровка CoM offset. */
  [[nodiscard]] bool IsComOffsetCalibActive() const override {
    return com_calib_.IsActive();
  }

  /** Результат калибровки CoM offset. */
  [[nodiscard]] ComOffsetCalibration::Result
  GetComOffsetCalibResult() const override {
    return com_calib_.GetResult();
  }

  /**
   * @brief Запустить автоматический тестовый манёвр
   * @param params Параметры теста
   * @return true при успешном запуске
   */
  bool StartTest(const TestParams& params) override;

  /** Прервать тестовый манёвр. */
  void StopTest() override { test_runner_.Stop(); }

  /** true пока тест активен. */
  [[nodiscard]] bool IsTestActive() const override {
    return test_runner_.IsActive();
  }

  /** Статус текущего теста. */
  [[nodiscard]] TestRunner::Status GetTestStatus() const override {
    return test_runner_.GetStatus();
  }

  /**
   * @brief Включить/выключить детский режим
   *
   * Обновляет StabilizationConfig.mode, что изменяет routing в control loop
   * через ModeTraits. Если IMU не инициализирован (stab_mgr_ == nullptr),
   * вызов игнорируется.
   *
   * @param active true — включить (DriveMode::Kids), false — Normal
   */
  void SetKidsModeActive(bool active) override {
    if (!stab_mgr_) return;
    auto cfg = stab_mgr_->GetConfig();
    cfg.mode = active ? DriveMode::Kids : DriveMode::Normal;
    stab_mgr_->SetConfig(cfg);
  }

  /**
   * @brief Проверить, активен ли детский режим
   * @return true если текущий режим == DriveMode::Kids
   */
  [[nodiscard]] bool IsKidsModeActive() const override {
    return stab_mgr_ && stab_mgr_->GetConfig().mode == DriveMode::Kids;
  }

  /**
   * @brief Получить текущую конфигурацию стабилизации
   * @return Конфигурация стабилизации
   */
  [[nodiscard]] StabilizationConfig GetStabilizationConfig() const override {
    return stab_mgr_->GetConfig();
  }

  /**
   * @brief Установить конфигурацию стабилизации
   * @param config Новая конфигурация
   * @param save_to_nvs Сохранить в NVS (по умолчанию true)
   * @return true при успехе
   */
  bool SetStabilizationConfig(const StabilizationConfig& config,
                              bool save_to_nvs = true) override {
    return stab_mgr_->SetConfig(config, save_to_nvs);
  }

  /**
   * @brief Получить информацию о буфере телеметрии
   * @param count_out Текущее количество кадров
   * @param cap_out   Ёмкость буфера
   */
  void GetLogInfo(size_t& count_out, size_t& cap_out) const override {
    telem_mgr_->GetLogInfo(count_out, cap_out);
  }

  /**
   * @brief Получить кадр телеметрии по индексу (0 = oldest)
   * @param idx Индекс кадра
   * @param out Выходной кадр
   * @return true если idx < Count()
   */
  bool GetLogFrame(size_t idx, TelemetryLogFrame& out) const override {
    return telem_mgr_->GetLogFrame(idx, out);
  }

  /**
   * @brief Очистить буфер телеметрии
   */
  void ClearLog() override { telem_mgr_->Clear(); }

  /**
   * @brief Запустить self-test (проверка подсистем)
   * @return Вектор результатов проверок
   */
  [[nodiscard]] std::vector<SelfTestItem> RunSelfTest() const override;

  /**
   * @brief Проверить, готов ли control loop к обработке команд
   *
   * Возвращает true после того, как control task завершит первую
   * итерацию. WS-хэндлеры должны проверять этот флаг перед
   * обработкой команд, чтобы не обращаться к неинициализированным
   * компонентам.
   */
  [[nodiscard]] bool IsReady() const noexcept override {
    return control_task_ready_.load(std::memory_order_acquire);
  }

  VehicleControlUnified(const VehicleControlUnified&) = delete;
  VehicleControlUnified& operator=(const VehicleControlUnified&) = delete;

 private:

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
   * @brief Выбор источника управления (RC приоритетнее Wi-Fi)
   * @param commanded_throttle Выходной параметр: газ
   * @param commanded_steering Выходной параметр: руль
   * @return true если есть активный источник управления
   */
  bool SelectControlSource(const SensorSnapshot& sensors,
                           float& commanded_throttle,
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
                             uint32_t& last_pwm_update,
                             float throttle_trim = 0.0f,
                             float steering_trim = 0.0f,
                             float slew_throttle_per_sec = 0.5f,
                             float slew_steering_per_sec = 3.0f);

  /**
   * @brief Вывод диагностической информации
   * @param now_ms Текущее время
   * @param diag_loop_count Счётчик итераций (in/out)
   * @param diag_start_ms Время начала диагностики (in/out)
   */
  void PrintDiagnostics(uint32_t now_ms, uint32_t& diag_loop_count,
                        uint32_t& diag_start_ms);

  /** @brief Формирование снимка телеметрии для WebSocket-стриминга */
  TelemetrySnapshot BuildTelemetrySnapshot(
      uint32_t now, const SensorSnapshot& sensors,
      const StabilizationConfig& stab_cfg, DriveMode drive_mode,
      float applied_throttle, float applied_steering,
      float commanded_throttle, float commanded_steering) const;

  /** @brief Формирование кадра для кольцевого буфера телеметрии */
  TelemetryLogFrame BuildLogFrame(uint32_t now, const SensorSnapshot& sensors,
                                  float applied_throttle,
                                  float applied_steering,
                                  float commanded_throttle,
                                  float commanded_steering) const;

  // ─────────────────────────────────────────────────────────────────────────
  // Члены класса
  // ─────────────────────────────────────────────────────────────────────────

  // Платформа (HAL)
  std::unique_ptr<VehicleControlPlatform> platform_;

  // Калибровка, фильтр
  ImuCalibration imu_calib_;
  MadgwickFilter madgwick_;

  // Стратегии стабилизации (pipeline)
  YawRateController yaw_ctrl_;
  PitchCompensator pitch_ctrl_;
  SlipAngleController slip_ctrl_;
  OversteerGuard oversteer_guard_;

  // Kids Mode процессор (ограничения газа/руля, anti-spin)
  KidsModeProcessor kids_processor_;

  // EKF оценки динамического состояния (vx, vy, r → slip angle)
  VehicleEkf ekf_;

  // Автокалибровка steering trim
  SteeringTrimCalibration trim_calib_;

  // Калибровка CoM offset (CW+CCW)
  ComOffsetCalibration com_calib_;

  // Тестовые автоматические манёвры
  TestRunner test_runner_;

  // Control components
  std::unique_ptr<RcInputHandler> rc_handler_;
  std::unique_ptr<WifiCommandHandler> wifi_handler_;
  std::unique_ptr<ImuHandler> imu_handler_;
  std::unique_ptr<TelemetryHandler> telem_handler_;

  // Флаги состояния
  bool rc_enabled_{false};
  bool imu_enabled_{false};
  bool inited_{false};

  // Предыдущее значение gz (рад/с) для вычисления углового ускорения α
  float prev_gz_rad_s_{0.0f};

  // Последнее измерение частоты loop (обновляется в PrintDiagnostics)
  std::atomic<uint32_t> last_loop_hz_{0};

  // Флаг готовности control task (init-ready barrier)
  std::atomic<bool> control_task_ready_{false};

  // Менеджеры (управление отдельными аспектами системы)
  std::unique_ptr<CalibrationManager> calib_mgr_;
  std::unique_ptr<StabilizationManager> stab_mgr_;
  std::unique_ptr<TelemetryManager> telem_mgr_;
};

}  // namespace rc_vehicle