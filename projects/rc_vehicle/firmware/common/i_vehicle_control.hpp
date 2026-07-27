#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "com_offset_calibration.hpp"
#include "self_test.hpp"
#include "speed_calibration.hpp"
#include "stabilization_config.hpp"
#include "steering_trim_calibration.hpp"
#include "telemetry_event_log.hpp"
#include "telemetry_log.hpp"
#include "test_runner.hpp"

namespace rc_vehicle {

/**
 * @brief Согласованный снимок состояния калибровки магнитометра.
 *
 * Строки — литералы со статическим временем жизни, копировать их не нужно.
 * erase_result — результат ПОСЛЕДНЕЙ команды erase ("none", пока её не было;
 * иначе "ok"/"failed"), т.к. само стирание NVS не меняет status/fail_reason
 * калибровки в памяти. erase_seq растёт на 1 при КАЖДОМ применении erase —
 * это единственный надёжный признак, что именно ЭТА команда завершилась:
 * erase_result двух последовательных erase может совпасть (оба "ok"), и
 * сравнение только по значению не отличило бы новое завершение от старого.
 */
struct MagCalibStateView {
  const char* status{"idle"};
  const char* fail_reason{"none"};
  const char* erase_result{"none"};
  uint32_t erase_seq{0};
};

/**
 * @brief Интерфейс управления машиной для WS-хендлеров и внешних модулей.
 *
 * Позволяет подставлять mock-реализацию в тестах без зависимости
 * от VehicleControlUnified и всего control loop.
 */
class IVehicleControl {
 public:
  virtual ~IVehicleControl() = default;

  // Команда управления
  virtual void OnWifiCommand(float throttle, float steering) = 0;

  // Калибровка
  virtual void StartCalibration(bool full) = 0;
  virtual bool StartForwardCalibration() = 0;
  virtual bool StartAutoForwardCalibration(float target_accel_g = 0.1f) = 0;
  [[nodiscard]] virtual const char* GetCalibStatus() const = 0;
  [[nodiscard]] virtual int GetCalibStage() const = 0;
  virtual void SetForwardDirection(float fx, float fy, float fz) = 0;

  // Конфигурация стабилизации
  [[nodiscard]] virtual StabilizationConfig GetStabilizationConfig() const = 0;
  virtual bool SetStabilizationConfig(const StabilizationConfig& config,
                                      bool save_to_nvs = true) = 0;

  // Kids mode
  virtual void SetKidsModeActive(bool active) = 0;
  [[nodiscard]] virtual bool IsKidsModeActive() const = 0;

  // Калибровка trim руля
  virtual bool StartSteeringTrimCalibration(float target_accel_g = 0.1f) = 0;
  virtual void StopSteeringTrimCalibration() = 0;
  [[nodiscard]] virtual bool IsSteeringTrimCalibActive() const = 0;
  [[nodiscard]] virtual SteeringTrimCalibration::Result
  GetSteeringTrimCalibResult() const = 0;

  // Калибровка CoM offset
  virtual bool StartComOffsetCalibration(float target_accel_g = 0.1f,
                                         float steering_magnitude = 0.5f,
                                         float cruise_duration_sec = 5.0f) = 0;
  virtual void StopComOffsetCalibration() = 0;
  [[nodiscard]] virtual bool IsComOffsetCalibActive() const = 0;
  [[nodiscard]] virtual ComOffsetCalibration::Result GetComOffsetCalibResult()
      const = 0;

  // Тестовые манёвры
  virtual bool StartTest(const TestParams& params) = 0;
  virtual void StopTest() = 0;
  [[nodiscard]] virtual bool IsTestActive() const = 0;
  [[nodiscard]] virtual TestRunner::Status GetTestStatus() const = 0;

  // Калибровка скорости (throttle → speed gain)
  virtual bool StartSpeedCalibration(float target_throttle = 0.3f,
                                     float cruise_duration_sec = 3.0f) = 0;
  virtual void StopSpeedCalibration() = 0;
  [[nodiscard]] virtual bool IsSpeedCalibActive() const = 0;
  [[nodiscard]] virtual SpeedCalibration::Result GetSpeedCalibResult()
      const = 0;

  // Относительный курс
  virtual void ResetHeadingRef() = 0;

  // ─── Калибровка магнитометра ─────────────────────────────────────────────
  //
  // Все четыре команды исполняются не на месте, а в порядке поступления на
  // потоке control loop (реализация решает, как именно). Возвращают НЕ
  // результат операции, а факт приёма: false означает, что очередь команд
  // переполнена и команда отброшена, — вызывающий обязан сообщить об этом
  // клиенту, иначе отброшенный cancel оставит калибровку собирать семплы
  // вечно, а клиент будет считать команду принятой (ревью PR #308).

  virtual bool StartMagCalibration() = 0;
  virtual bool FinishMagCalibration() = 0;
  virtual bool CancelMagCalibration() = 0;

  /**
   * @brief Стереть калибровку магнитометра из NVS.
   *
   * Тоже через очередь: иначе erase, пришедший сразу за finish, стирал бы
   * NVS ДО того, как отложенный finish запишет туда только что посчитанную
   * калибровку — и клиент получал бы ok на стирание, после которого
   * калибровка на месте (ревью PR #308).
   */
  virtual bool EraseMagCalibration() = 0;

  /**
   * @brief Статус калибровки магнитометра и причина неудачи — одним снимком.
   *
   * Именно парой, а не двумя геттерами: статус меняется с потока control
   * loop, а читают его WS-обработчики, поэтому два раздельных чтения (пусть
   * даже каждое под мьютексом) успевают разъехаться и дать наружу
   * несуществовавшее сочетание — "collecting" с "too_few_samples" или
   * "failed" с "none" (ревью PR #308).
   */
  [[nodiscard]] virtual MagCalibStateView GetMagCalibState() const = 0;

  // Телеметрия лог (кадры)
  virtual void GetLogInfo(size_t& count_out, size_t& cap_out) const = 0;
  [[nodiscard]] virtual bool GetLogFrame(size_t idx,
                                         TelemetryLogFrame& out) const = 0;
  virtual void ClearLog() = 0;

  // Лог событий (старт/стоп режимов и калибровок)
  [[nodiscard]] virtual size_t GetEventCount() const = 0;
  [[nodiscard]] virtual bool GetEvent(size_t idx,
                                      TelemetryEvent& out) const = 0;
  virtual void ClearEventLog() = 0;

  // Диагностика
  [[nodiscard]] virtual std::vector<SelfTestItem> RunSelfTest() const = 0;
  [[nodiscard]] virtual bool IsReady() const noexcept = 0;
};

}  // namespace rc_vehicle
