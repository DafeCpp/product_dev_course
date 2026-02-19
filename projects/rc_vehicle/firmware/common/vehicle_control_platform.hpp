#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "imu_calibration.hpp"
#include "mpu6050_spi.hpp"

namespace rc_vehicle {

/**
 * @brief Ошибки инициализации платформы
 */
enum class PlatformError : uint8_t {
  Ok = 0,
  PwmInitFailed,
  RcInitFailed,
  ImuInitFailed,
  TaskCreateFailed,
  CalibLoadFailed,
  CalibSaveFailed,
  FailsafeInitFailed
};

/**
 * @brief Уровни логирования
 */
enum class LogLevel : uint8_t { Info = 0, Warning, Error };

/**
 * @brief Команда управления (RC или WiFi)
 */
struct RcCommand {
  float throttle{0.0f};  ///< Газ [-1..1]
  float steering{0.0f};  ///< Руль [-1..1]
};

/**
 * @brief Абстрактный интерфейс платформы для VehicleControl
 *
 * Предоставляет HAL для доступа к аппаратным ресурсам (PWM, RC, IMU, NVS)
 * и платформенным сервисам (логирование, время, задачи, WebSocket).
 *
 * Реализация предоставляется целевой платформой (ESP32-S3, ESP32-C6, RP2040,
 * STM32).
 *
 * @note Все методы должны быть потокобезопасными, если вызываются из разных
 * задач.
 */
class VehicleControlPlatform {
 public:
  virtual ~VehicleControlPlatform() = default;

  // ─────────────────────────────────────────────────────────────────────────
  // Инициализация
  // ─────────────────────────────────────────────────────────────────────────

  /**
   * @brief Инициализация PWM-выходов (throttle, steering)
   * @return PlatformError::Ok при успехе
   */
  [[nodiscard]] virtual PlatformError InitPwm() = 0;

  /**
   * @brief Инициализация RC-входов (PWM capture)
   * @return PlatformError::Ok при успехе
   */
  [[nodiscard]] virtual PlatformError InitRc() = 0;

  /**
   * @brief Инициализация IMU (SPI, I2C)
   * @return PlatformError::Ok при успехе
   */
  [[nodiscard]] virtual PlatformError InitImu() = 0;

  /**
   * @brief Инициализация failsafe
   * @return PlatformError::Ok при успехе
   */
  [[nodiscard]] virtual PlatformError InitFailsafe() = 0;

  // ─────────────────────────────────────────────────────────────────────────
  // Время
  // ─────────────────────────────────────────────────────────────────────────

  /**
   * @brief Текущее время в миллисекундах
   * @return Монотонное время с момента старта системы
   */
  [[nodiscard]] virtual uint32_t GetTimeMs() const noexcept = 0;

  /**
   * @brief Текущее время в микросекундах (для диагностики)
   * @return Монотонное время с момента старта системы
   */
  [[nodiscard]] virtual uint64_t GetTimeUs() const noexcept = 0;

  // ─────────────────────────────────────────────────────────────────────────
  // Логирование
  // ─────────────────────────────────────────────────────────────────────────

  /**
   * @brief Вывод лог-сообщения
   * @param level Уровень важности
   * @param msg Текст сообщения (UTF-8)
   */
  virtual void Log(LogLevel level, std::string_view msg) const = 0;

  // ─────────────────────────────────────────────────────────────────────────
  // IMU
  // ─────────────────────────────────────────────────────────────────────────

  /**
   * @brief Прочитать данные IMU
   * @return Данные акселерометра и гироскопа, если чтение успешно
   */
  [[nodiscard]] virtual std::optional<ImuData> ReadImu() = 0;

  /**
   * @brief Получить последнее значение WHO_AM_I регистра IMU
   * @return Значение регистра или -1, если не читали
   */
  [[nodiscard]] virtual int GetImuLastWhoAmI() const noexcept = 0;

  // ─────────────────────────────────────────────────────────────────────────
  // Калибровка IMU
  // ─────────────────────────────────────────────────────────────────────────

  /**
   * @brief Загрузить калибровку IMU из энергонезависимой памяти
   * @return Данные калибровки, если найдены и валидны
   */
  [[nodiscard]] virtual std::optional<ImuCalibData> LoadCalib() = 0;

  /**
   * @brief Сохранить калибровку IMU в энергонезависимую память
   * @param data Данные калибровки
   * @return true при успешном сохранении
   */
  [[nodiscard]] virtual bool SaveCalib(const ImuCalibData& data) = 0;

  // ─────────────────────────────────────────────────────────────────────────
  // RC Input
  // ─────────────────────────────────────────────────────────────────────────

  /**
   * @brief Прочитать команду от RC-приёмника
   * @return Команда управления, если оба канала (throttle, steering) валидны
   */
  [[nodiscard]] virtual std::optional<RcCommand> GetRc() = 0;

  // ─────────────────────────────────────────────────────────────────────────
  // PWM Output
  // ─────────────────────────────────────────────────────────────────────────

  /**
   * @brief Установить PWM-выходы
   * @param throttle Газ, нормализованный [-1..1]
   * @param steering Руль, нормализованный [-1..1]
   */
  virtual void SetPwm(float throttle, float steering) noexcept = 0;

  /**
   * @brief Установить нейтральное положение (failsafe)
   */
  virtual void SetPwmNeutral() noexcept = 0;

  // ─────────────────────────────────────────────────────────────────────────
  // Failsafe
  // ─────────────────────────────────────────────────────────────────────────

  /**
   * @brief Обновить состояние failsafe
   * @param rc_active RC-приёмник активен
   * @param wifi_active Wi-Fi команды активны
   * @return true, если failsafe активен (нет управления)
   */
  [[nodiscard]] virtual bool FailsafeUpdate(bool rc_active,
                                            bool wifi_active) = 0;

  /**
   * @brief Проверить, активен ли failsafe
   * @return true, если failsafe активен
   */
  [[nodiscard]] virtual bool FailsafeIsActive() const noexcept = 0;

  // ─────────────────────────────────────────────────────────────────────────
  // WebSocket (только для ESP32)
  // ─────────────────────────────────────────────────────────────────────────

  /**
   * @brief Получить количество подключённых WebSocket-клиентов
   * @return Количество клиентов (0 для платформ без WebSocket)
   */
  [[nodiscard]] virtual unsigned GetWebSocketClientCount() const noexcept = 0;

  /**
   * @brief Отправить телеметрию по WebSocket
   * @param json JSON-строка с телеметрией
   */
  virtual void SendTelem(std::string_view json) = 0;

  // ─────────────────────────────────────────────────────────────────────────
  // Wi-Fi команды (только для ESP32)
  // ─────────────────────────────────────────────────────────────────────────

  /**
   * @brief Попытаться получить команду из очереди Wi-Fi (неблокирующе)
   * @return Команда, если была в очереди
   */
  [[nodiscard]] virtual std::optional<RcCommand> TryReceiveWifiCommand() = 0;

  /**
   * @brief Поставить команду в очередь Wi-Fi (вызов из потока WebSocket)
   * @param throttle Газ [-1..1]
   * @param steering Руль [-1..1]
   */
  virtual void SendWifiCommand(float throttle, float steering) = 0;

  // ─────────────────────────────────────────────────────────────────────────
  // Задачи и синхронизация
  // ─────────────────────────────────────────────────────────────────────────

  /**
   * @brief Создать задачу control loop
   * @param entry Функция-точка входа задачи
   * @param arg Аргумент для передачи в entry
   * @return true при успешном создании задачи
   */
  [[nodiscard]] virtual bool CreateTask(void (*entry)(void*), void* arg) = 0;

  /**
   * @brief Задержка до следующего тика (для периодических задач)
   * @param period_ms Период в миллисекундах от предыдущего пробуждения
   */
  virtual void DelayUntilNextTick(uint32_t period_ms) = 0;
};

}  // namespace rc_vehicle
