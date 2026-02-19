#pragma once

#include <cstdint>
#include <optional>

#include "imu_calibration.hpp"
#include "madgwick_filter.hpp"
#include "vehicle_control_platform.hpp"

namespace rc_vehicle {

/**
 * @brief Базовый интерфейс для компонентов control loop
 *
 * Каждый компонент отвечает за одну область функциональности
 * (RC input, Wi-Fi команды, IMU, телеметрия) и обновляется периодически.
 */
class ControlComponent {
 public:
  virtual ~ControlComponent() = default;

  /**
   * @brief Обновить состояние компонента
   * @param now_ms Текущее время в миллисекундах
   * @param dt_ms Время с последнего обновления в миллисекундах
   */
  virtual void Update(uint32_t now_ms, uint32_t dt_ms) = 0;
};

// ═════════════════════════════════════════════════════════════════════════
// RC Input Handler
// ═════════════════════════════════════════════════════════════════════════

/**
 * @brief Обработчик RC-входа
 *
 * Опрашивает RC-приёмник с заданной частотой и предоставляет
 * последнюю валидную команду управления.
 */
class RcInputHandler : public ControlComponent {
 public:
  /**
   * @brief Конструктор
   * @param platform Платформа для доступа к RC-входу
   * @param poll_interval_ms Интервал опроса в миллисекундах (по умолчанию 20 ms
   * = 50 Hz)
   */
  explicit RcInputHandler(VehicleControlPlatform& platform,
                          uint32_t poll_interval_ms = 20)
      : platform_(platform), poll_interval_ms_(poll_interval_ms) {}

  void Update(uint32_t now_ms, uint32_t dt_ms) override;

  /**
   * @brief Проверить, активен ли RC-вход
   * @return true, если получены валидные сигналы
   */
  [[nodiscard]] bool IsActive() const noexcept { return active_; }

  /**
   * @brief Получить последнюю команду
   * @return Команда управления, если RC активен
   */
  [[nodiscard]] std::optional<RcCommand> GetCommand() const {
    return active_ ? last_command_ : std::nullopt;
  }

 private:
  VehicleControlPlatform& platform_;
  uint32_t poll_interval_ms_;
  uint32_t last_poll_ms_{0};
  bool active_{false};
  std::optional<RcCommand> last_command_;
};

// ═════════════════════════════════════════════════════════════════════════
// Wi-Fi Command Handler
// ═════════════════════════════════════════════════════════════════════════

/**
 * @brief Обработчик Wi-Fi команд
 *
 * Получает команды из очереди WebSocket и отслеживает их актуальность
 * (команды старше timeout_ms считаются устаревшими).
 */
class WifiCommandHandler : public ControlComponent {
 public:
  /**
   * @brief Конструктор
   * @param platform Платформа для доступа к Wi-Fi командам
   * @param timeout_ms Таймаут актуальности команды (по умолчанию 500 ms)
   */
  explicit WifiCommandHandler(VehicleControlPlatform& platform,
                              uint32_t timeout_ms = 500)
      : platform_(platform), timeout_ms_(timeout_ms) {}

  void Update(uint32_t now_ms, uint32_t dt_ms) override;

  /**
   * @brief Проверить, активны ли Wi-Fi команды
   * @return true, если команда получена недавно (в пределах timeout)
   */
  [[nodiscard]] bool IsActive() const noexcept {
    return last_cmd_ms_ != 0 &&
           (platform_.GetTimeMs() - last_cmd_ms_) < timeout_ms_;
  }

  /**
   * @brief Получить последнюю команду
   * @return Команда управления, если Wi-Fi активен
   */
  [[nodiscard]] std::optional<RcCommand> GetCommand() const {
    return IsActive() ? last_command_ : std::nullopt;
  }

 private:
  VehicleControlPlatform& platform_;
  uint32_t timeout_ms_;
  uint32_t last_cmd_ms_{0};
  std::optional<RcCommand> last_command_;
};

// ═════════════════════════════════════════════════════════════════════════
// IMU Handler (ImuCalibration, MadgwickFilter — из imu_calibration.hpp, madgwick_filter.hpp, глобальный namespace)
// ═════════════════════════════════════════════════════════════════════════

/**
 * @brief Обработчик IMU
 *
 * Читает данные IMU с заданной частотой, применяет калибровку
 * и обновляет фильтр ориентации.
 */
class ImuHandler : public ControlComponent {
 public:
  /**
   * @brief Конструктор
   * @param platform Платформа для доступа к IMU
   * @param calib Калибровка IMU
   * @param filter Фильтр Madgwick для ориентации
   * @param read_interval_ms Интервал чтения в миллисекундах (по умолчанию 2 ms
   * = 500 Hz)
   */
  ImuHandler(VehicleControlPlatform& platform, ImuCalibration& calib,
             MadgwickFilter& filter, uint32_t read_interval_ms = 2)
      : platform_(platform),
        calib_(calib),
        filter_(filter),
        read_interval_ms_(read_interval_ms) {}

  void Update(uint32_t now_ms, uint32_t dt_ms) override;

  /**
   * @brief Получить последние данные IMU
   * @return Данные акселерометра и гироскопа
   */
  [[nodiscard]] const ImuData& GetData() const noexcept { return data_; }

  /**
   * @brief Проверить, включен ли IMU
   * @return true, если IMU инициализирован и работает
   */
  [[nodiscard]] bool IsEnabled() const noexcept { return enabled_; }

  /**
   * @brief Включить/выключить IMU
   * @param enabled Новое состояние
   */
  void SetEnabled(bool enabled) noexcept { enabled_ = enabled; }

 private:
  VehicleControlPlatform& platform_;
  ImuCalibration& calib_;
  MadgwickFilter& filter_;
  uint32_t read_interval_ms_;
  uint32_t last_read_ms_{0};
  ImuData data_{};
  bool enabled_{false};
};

// ═════════════════════════════════════════════════════════════════════════
// Telemetry Handler
// ═════════════════════════════════════════════════════════════════════════

/**
 * @brief Обработчик телеметрии
 *
 * Собирает данные от других компонентов и отправляет телеметрию
 * по WebSocket с заданной частотой.
 */
class TelemetryHandler : public ControlComponent {
 public:
  /**
   * @brief Конструктор
   * @param platform Платформа для отправки телеметрии
   * @param rc RC input handler
   * @param wifi Wi-Fi command handler
   * @param imu IMU handler
   * @param calib Калибровка IMU
   * @param filter Фильтр Madgwick
   * @param send_interval_ms Интервал отправки в миллисекундах (по умолчанию 50
   * ms = 20 Hz)
   */
  TelemetryHandler(VehicleControlPlatform& platform, const RcInputHandler& rc,
                   const WifiCommandHandler& wifi, const ImuHandler& imu,
                   const ImuCalibration& calib, const MadgwickFilter& filter,
                   uint32_t send_interval_ms = 50)
      : platform_(platform),
        rc_(rc),
        wifi_(wifi),
        imu_(imu),
        calib_(calib),
        filter_(filter),
        send_interval_ms_(send_interval_ms) {}

  void Update(uint32_t now_ms, uint32_t dt_ms) override;

  /**
   * @brief Установить значения актуаторов (для отправки в телеметрии)
   * @param throttle Газ [-1..1]
   * @param steering Руль [-1..1]
   */
  void SetActuatorValues(float throttle, float steering) noexcept {
    applied_throttle_ = throttle;
    applied_steering_ = steering;
  }

 private:
  VehicleControlPlatform& platform_;
  const RcInputHandler& rc_;
  const WifiCommandHandler& wifi_;
  const ImuHandler& imu_;
  const ImuCalibration& calib_;
  const MadgwickFilter& filter_;
  uint32_t send_interval_ms_;
  uint32_t last_send_ms_{0};
  float applied_throttle_{0.0f};
  float applied_steering_{0.0f};

  /**
   * @brief Построить JSON-строку с телеметрией
   * @return JSON-строка
   */
  [[nodiscard]] std::string BuildTelemJson() const;
};

}  // namespace rc_vehicle