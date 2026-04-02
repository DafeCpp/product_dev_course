#pragma once

#include <atomic>
#include <memory>

#include "auto_drive_coordinator.hpp"
#include "calibration_manager.hpp"
#include "control_components.hpp"
#include "drive_mode_registry.hpp"
#include "i_vehicle_control.hpp"
#include "imu_calibration.hpp"
#include "mag_calibration.hpp"
#include "self_test.hpp"
#include "kids_mode_processor.hpp"
#include "madgwick_filter.hpp"
#include "stabilization_config.hpp"
#include "stabilization_manager.hpp"
#include "stabilization_pipeline.hpp"
#include "telemetry_manager.hpp"
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

  // ─── Относительный курс ──────────────────────────────────────────────────

  /** Сбросить опорный курс (установится при следующем Update() с магнитометром). */
  void ResetHeadingRef() override {
    if (imu_handler_) imu_handler_->ResetHeadingRef();
  }

  // ─── Калибровка магнитометра ─────────────────────────────────────────────

  /** Запустить сбор семплов калибровки магнитометра. */
  void StartMagCalibration() override { mag_calib_.Start(); }

  /** Завершить сбор, вычислить offset и сохранить в NVS (если валидно). */
  void FinishMagCalibration() override {
    mag_calib_.Finish();
    if (mag_calib_.IsValid()) {
      platform_->SaveMagCalib(mag_calib_.GetData());
    }
  }

  /** Прервать сбор, вернуться в Idle. */
  void CancelMagCalibration() override { mag_calib_.Cancel(); }

  /** Строковый статус калибровки магнитометра. */
  [[nodiscard]] const char* GetMagCalibStatus() const override {
    switch (mag_calib_.GetStatus()) {
      case MagCalibStatus::Idle:       return "idle";
      case MagCalibStatus::Collecting: return "collecting";
      case MagCalibStatus::Done:       return "done";
      case MagCalibStatus::Failed:     return "failed";
    }
    return "idle";
  }

  /** Удалить калибровку магнитометра из NVS. */
  bool EraseMagCalibration() override {
    return platform_->EraseMagCalib();
  }

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
  void StopSteeringTrimCalibration() override { auto_drive_.StopTrimCalib(); }

  /** true пока идёт калибровка trim. */
  [[nodiscard]] bool IsSteeringTrimCalibActive() const override {
    return auto_drive_.IsTrimCalibActive();
  }

  /** Результат калибровки trim (валиден после завершения). */
  [[nodiscard]] SteeringTrimCalibration::Result
  GetSteeringTrimCalibResult() const override {
    return auto_drive_.GetTrimCalibResult();
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
  void StopComOffsetCalibration() override { auto_drive_.StopComCalib(); }

  /** true пока идёт калибровка CoM offset. */
  [[nodiscard]] bool IsComOffsetCalibActive() const override {
    return auto_drive_.IsComCalibActive();
  }

  /** Результат калибровки CoM offset. */
  [[nodiscard]] ComOffsetCalibration::Result
  GetComOffsetCalibResult() const override {
    return auto_drive_.GetComCalibResult();
  }

  /**
   * @brief Запустить автоматический тестовый манёвр
   * @param params Параметры теста
   * @return true при успешном запуске
   */
  bool StartTest(const TestParams& params) override;

  /** Прервать тестовый манёвр. */
  void StopTest() override { auto_drive_.StopTest(); }

  /** true пока тест активен. */
  [[nodiscard]] bool IsTestActive() const override {
    return auto_drive_.IsTestActive();
  }

  /** Статус текущего теста. */
  [[nodiscard]] TestRunner::Status GetTestStatus() const override {
    return auto_drive_.GetTestStatus();
  }

  /**
   * @brief Запустить калибровку скорости (throttle → speed gain)
   * @param target_throttle Целевой газ в фазе крейсера
   * @param cruise_duration_sec Длительность крейсерской фазы
   * @return true при успешном запуске
   */
  bool StartSpeedCalibration(float target_throttle = 0.3f,
                              float cruise_duration_sec = 3.0f) override;

  /** Прервать калибровку скорости. */
  void StopSpeedCalibration() override { auto_drive_.StopSpeedCalib(); }

  /** true пока идёт калибровка скорости. */
  [[nodiscard]] bool IsSpeedCalibActive() const override {
    return auto_drive_.IsSpeedCalibActive();
  }

  /** Результат калибровки скорости. */
  [[nodiscard]] SpeedCalibration::Result GetSpeedCalibResult() const override {
    return auto_drive_.GetSpeedCalibResult();
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

  /** Инициализация IMU подсистемы (менеджеры, NVS, авто-калибровка). */
  void InitImuSubsystem();

  /** Инициализация кольцевого буфера телеметрии. */
  void InitTelemetryLog();

  /** Создание компонентов control loop. */
  bool InitializeComponents();



  // ─────────────────────────────────────────────────────────────────────────
  // Члены класса
  // ─────────────────────────────────────────────────────────────────────────

  // Платформа (HAL)
  std::unique_ptr<VehicleControlPlatform> platform_;

  // Калибровка, фильтр
  ImuCalibration imu_calib_;
  MagCalibration mag_calib_;
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

  // Координатор авто-процедур (trim calib, CoM calib, test runner)
  AutoDriveCoordinator auto_drive_;

  // Control components
  std::unique_ptr<RcInputHandler> rc_handler_;
  std::unique_ptr<WifiCommandHandler> wifi_handler_;
  std::unique_ptr<ImuHandler> imu_handler_;
  std::unique_ptr<TelemetryHandler> telem_handler_;

  // Флаги состояния
  bool rc_enabled_{false};
  bool imu_enabled_{false};
  bool inited_{false};

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