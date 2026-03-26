#pragma once

#include <cstdint>
#include <optional>

#include "config.hpp"
#include "imu_calibration.hpp"
#include "lpf_butterworth.hpp"
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
  [[nodiscard]] bool IsActive() const noexcept { return active_; }

  /**
   * @brief Получить последнюю команду
   * @return Команда управления, если Wi-Fi активен
   */
  [[nodiscard]] std::optional<RcCommand> GetCommand() const {
    return active_ ? last_command_ : std::nullopt;
  }

 private:
  VehicleControlPlatform& platform_;
  uint32_t timeout_ms_;
  uint32_t last_cmd_ms_{0};
  bool active_{false};
  std::optional<RcCommand> last_command_;
};

// ═════════════════════════════════════════════════════════════════════════
// IMU Handler (ImuCalibration, MadgwickFilter — из imu_calibration.hpp,
// madgwick_filter.hpp, глобальный namespace)
// ═════════════════════════════════════════════════════════════════════════

/**
 * @brief Обработчик IMU
 *
 * Читает данные IMU с заданной частотой, применяет калибровку
 * и обновляет фильтр ориентации. Gyro Z фильтруется LPF Butterworth 2-го
 * порядка для последующего использования в ПИД контроля рыскания.
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
        read_interval_ms_(read_interval_ms) {
    const float fs_hz = 1000.f / static_cast<float>(read_interval_ms_);
    lpf_gyro_z_.SetParams(config::LpfConfig::kDefaultCutoffHz, fs_hz);
  }

  void Update(uint32_t now_ms, uint32_t dt_ms) override;

  /**
   * @brief Установить частоту среза LPF для gyro Z
   * @param cutoff_hz Частота среза в Hz (5-100)
   */
  void SetLpfCutoff(float cutoff_hz);

  /**
   * @brief Получить последние данные IMU
   * @return Данные акселерометра и гироскопа
   */
  [[nodiscard]] const ImuData& GetData() const noexcept { return data_; }

  /**
   * @brief Отфильтрованная угловая скорость по оси Z (dps), LPF Butterworth
   * 2-го порядка.
   * @return Значение после калибровки и LPF; 0 если IMU выключен или фильтр не
   * настроен.
   */
  [[nodiscard]] float GetFilteredGyroZ() const noexcept { return filtered_gz_; }

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

  /**
   * @brief Включить/выключить обновление фильтра Madgwick
   * @param enabled false — IMU читается и LPF работает, но Madgwick не обновляется
   */
  void SetMadgwickEnabled(bool enabled) noexcept { madgwick_enabled_ = enabled; }

 private:
  VehicleControlPlatform& platform_;
  ImuCalibration& calib_;
  MadgwickFilter& filter_;
  uint32_t read_interval_ms_;
  uint32_t last_read_ms_{0};
  bool first_read_{true};
  ImuData data_{};
  bool enabled_{false};
  bool madgwick_enabled_{true};
  LpfButterworth2 lpf_gyro_z_{};
  float filtered_gz_{0.f};
  bool veh_frame_set_{false};  ///< Vehicle frame уже передан в фильтр
};

// ═════════════════════════════════════════════════════════════════════════
// SensorSnapshot — атомарный снимок состояния датчиков
// ═════════════════════════════════════════════════════════════════════════

/**
 * @brief Атомарный снимок состояния всех датчиков за одну итерацию control loop
 *
 * Собирается один раз после Update() всех компонентов и используется
 * во всех последующих этапах итерации. Предотвращает многократные вызовы
 * IsActive()/GetData()/GetFilteredGyroZ() и гарантирует согласованность данных.
 */
struct SensorSnapshot {
  // RC input
  bool rc_active{false};
  std::optional<RcCommand> rc_cmd;

  // Wi-Fi
  bool wifi_active{false};
  std::optional<RcCommand> wifi_cmd;

  // IMU
  bool imu_enabled{false};
  ImuData imu_data{};
  float filtered_gz{0.0f};
};

// ═════════════════════════════════════════════════════════════════════════
// TelemetrySnapshot
// ═════════════════════════════════════════════════════════════════════════

/**
 * @brief Снимок данных для телеметрии
 *
 * Заполняется в ControlTaskLoop() и передаётся в TelemetryHandler::SendTelemetry(),
 * устраняя прямые зависимости от отдельных компонентов.
 */
struct TelemetrySnapshot {
  // Link status
  bool rc_ok{false};
  bool wifi_ok{false};

  // IMU
  bool imu_enabled{false};
  ImuData imu_data{};
  float filtered_gz{0.0f};
  float forward_accel{0.0f};
  float pitch_deg{0.0f};
  float roll_deg{0.0f};
  float yaw_deg{0.0f};

  // Calibration
  CalibStatus calib_status{CalibStatus::Idle};
  int calib_stage{0};
  bool calib_valid{false};
  ImuCalibData calib_data{};

  // EKF (имеет смысл только при imu_enabled)
  bool ekf_available{false};
  float ekf_vx{0.0f};
  float ekf_vy{0.0f};
  float ekf_yaw_rate{0.0f};
  float ekf_slip_deg{0.0f};
  float ekf_speed_ms{0.0f};

  // Oversteer (имеет смысл только при imu_enabled)
  bool oversteer_available{false};
  bool oversteer_active{false};

  // Kids Mode
  bool kids_mode_active{false};
  bool kids_anti_spin_active{false};
  float kids_throttle_limit{0.0f};

  // RC input (сырые значения с пульта, до стабилизации)
  float rc_throttle{0.0f};
  float rc_steering{0.0f};

  // Actuators (applied, после стабилизации/trim/slew rate)
  float throttle{0.0f};
  float steering{0.0f};

  // Commanded (до trim/slew rate, после стабилизации)
  float cmd_throttle{0.0f};
  float cmd_steering{0.0f};

  // EKF covariance (для оценки качества фильтра)
  float ekf_vx_var{0.0f};
  float ekf_vy_var{0.0f};
  float ekf_r_var{0.0f};

  // Uptime (ms since boot, for reboot diagnostics)
  uint32_t uptime_ms{0};
};

// ═════════════════════════════════════════════════════════════════════════
// Telemetry Handler
// ═════════════════════════════════════════════════════════════════════════

/**
 * @brief Обработчик телеметрии
 *
 * Получает снимок данных (TelemetrySnapshot) и отправляет телеметрию
 * по WebSocket с заданной частотой.
 *
 * Не наследует ControlComponent — использует собственный интерфейс
 * SendTelemetry() вместо Update(now, dt).
 */
class TelemetryHandler {
 public:
  /**
   * @brief Конструктор
   * @param platform Платформа для отправки телеметрии
   * @param send_interval_ms Интервал отправки в миллисекундах (по умолчанию 50
   * ms = 20 Hz)
   */
  TelemetryHandler(VehicleControlPlatform& platform,
                   uint32_t send_interval_ms = 50)
      : platform_(platform), send_interval_ms_(send_interval_ms) {}

  /**
   * @brief Отправить телеметрию с переданным снимком данных
   * @param now_ms Текущее время в миллисекундах
   * @param snap Снимок данных телеметрии
   */
  void SendTelemetry(uint32_t now_ms, const TelemetrySnapshot& snap);

 private:
  VehicleControlPlatform& platform_;
  uint32_t send_interval_ms_;
  uint32_t last_send_ms_{0};

  /**
   * @brief Построить JSON-строку с телеметрией
   * @param snap Снимок данных
   * @return JSON-строка
   */
  [[nodiscard]] std::string BuildTelemJson(const TelemetrySnapshot& snap) const;
};

}  // namespace rc_vehicle