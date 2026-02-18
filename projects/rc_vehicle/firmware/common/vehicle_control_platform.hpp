#pragma once

#include <cstdint>
#include <optional>

#include "imu_calibration.hpp"
#include "mpu6050_spi.hpp"

/** Команда управления (RC или WiFi). */
struct RcCommand {
  float throttle;
  float steering;
};

/**
 * Платформенный HAL для VehicleControl.
 * Реализация предоставляется целевой платформой (esp32_s3, esp32_c6 и т.д.).
 * Общий код вызывает эти функции вместо прямого доступа к PWM/IMU/FreeRTOS.
 */
struct VehicleControlPlatform {
  /** Инициализация PWM. 0 — успех. */
  int (*InitPwm)(void);
  /** Инициализация RC-in. 0 — успех. */
  int (*InitRc)(void);
  /** Инициализация IMU. 0 — успех. */
  int (*InitImu)(void);
  /** Лог: level 0=info, 1=warn, 2=error. */
  void (*Log)(int level, const char* msg);
  /** Текущее время, мс. */
  uint32_t (*GetTimeMs)(void);
  /** Текущее время, мкс (для диагностики). */
  uint64_t (*GetTimeUs)(void);
  /** Прочитать IMU. Возвращает данные если успешно. */
  std::optional<ImuData> (*ReadImu)(void);
  /** Последний WHO_AM_I IMU (-1 если не читали). */
  int (*GetImuLastWhoAmI)(void);
  /** Загрузить калибровку из NVS/хранилища. Возвращает данные если успешно. */
  std::optional<ImuCalibData> (*LoadCalib)(void);
  /** Сохранить калибровку. true — успех. */
  bool (*SaveCalib)(const ImuCalibData& data);
  /** Прочитать RC; возвращает команду если оба канала валидны. */
  std::optional<RcCommand> (*GetRc)(void);
  /** Установить PWM (нормализованные [-1..1]). */
  void (*SetPwm)(float throttle, float steering);
  /** Нейтраль (failsafe). */
  void (*SetPwmNeutral)(void);
  /** Инициализация failsafe. */
  void (*FailsafeInit)(void);
  /** Обновить failsafe; true если активен. */
  bool (*FailsafeUpdate)(bool rc_active, bool wifi_active);
  /** Failsafe активен. */
  bool (*FailsafeIsActive)(void);
  /** Количество подключённых WebSocket-клиентов. */
  unsigned (*GetWebSocketClientCount)(void);
  /** Отправить телеметрию (JSON-строка). */
  void (*SendTelem)(const char* json);
  /** Забрать команду из очереди Wi‑Fi (неблокирующе). Возвращает команду если
   * была. */
  std::optional<RcCommand> (*TryReceiveWifiCommand)(void);
  /** Поставить команду в очередь (вызов из потока WebSocket). */
  void (*SendWifiCommand)(float throttle, float steering);
  /** Запустить задачу control loop. entry(arg) вызывается в цикле; после entry
   * возврата — delay. */
  bool (*CreateTask)(void (*entry)(void*), void* arg);
  /** Задержка до следующего тика (period_ms от предыдущего пробуждения). */
  void (*DelayUntilNextTick)(uint32_t period_ms);
};
