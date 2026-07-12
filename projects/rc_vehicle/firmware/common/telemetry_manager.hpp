#pragma once

#include <cstddef>
#include <cstdint>

#include "telemetry_event_log.hpp"
#include "telemetry_log.hpp"

namespace rc_vehicle {

/**
 * @brief Менеджер телеметрии
 *
 * Отвечает за:
 * - Управление кольцевым буфером телеметрии
 * - Запись кадров телеметрии
 * - Предоставление доступа к логам
 * - Очистку буфера
 *
 * Извлечён из VehicleControlUnified для соблюдения Single Responsibility
 * Principle.
 */
class TelemetryManager {
 public:
  TelemetryManager() = default;
  ~TelemetryManager() = default;

  TelemetryManager(const TelemetryManager&) = delete;
  TelemetryManager& operator=(const TelemetryManager&) = delete;

  /**
   * @brief Инициализировать буфер телеметрии
   * @param capacity_frames Максимальное количество кадров
   * @return true при успешном выделении памяти
   */
  bool Init(size_t capacity_frames);

  /**
   * @brief Записать кадр в буфер (вытесняет старые при переполнении)
   * @param frame Кадр телеметрии
   */
  void Push(const TelemetryLogFrame& frame);

  /**
   * @brief Получить информацию о буфере телеметрии
   * @param count_out Текущее количество кадров
   * @param cap_out   Ёмкость буфера
   */
  void GetLogInfo(size_t& count_out, size_t& cap_out) const {
    count_out = telem_log_.Count();
    cap_out = telem_log_.Capacity();
  }

  /**
   * @brief Получить кадр телеметрии по индексу (0 = oldest)
   * @param idx Индекс кадра
   * @param out Выходной кадр
   * @return true если idx < Count()
   */
  [[nodiscard]] bool GetLogFrame(size_t idx, TelemetryLogFrame& out) const {
    return telem_log_.GetFrame(idx, out);
  }

  /**
   * @brief Очистить буфер телеметрии
   */
  void Clear() { telem_log_.Clear(); }

  /**
   * @brief Получить время последней записи
   * @return Время последней записи в мс
   */
  [[nodiscard]] uint32_t GetLastLogTime() const { return last_log_ms_; }

  /**
   * @brief Установить время последней записи
   * @param time_ms Время в мс
   */
  void SetLastLogTime(uint32_t time_ms) { last_log_ms_ = time_ms; }

  /**
   * @brief Сбросить время последней записи (при failsafe)
   */
  void ResetLastLogTime() { last_log_ms_ = 0; }

  // ── Лог событий (старт/стоп режимов и калибровок) ─────────────────────────

  /**
   * @brief Записать событие в лог событий
   */
  void PushEvent(const TelemetryEvent& evt) { event_log_.Push(evt); }

  /**
   * @brief Получить количество событий в логе
   */
  [[nodiscard]] size_t GetEventCount() const { return event_log_.Count(); }

  /**
   * @brief Получить событие по индексу (0 = oldest)
   * @param idx Индекс события
   * @param out Выходное событие
   * @return true если idx < Count()
   */
  [[nodiscard]] bool GetEvent(size_t idx, TelemetryEvent& out) const {
    return event_log_.GetEvent(idx, out);
  }

  /**
   * @brief Очистить лог событий
   */
  void ClearEvents() { event_log_.Clear(); }

  /**
   * @brief Получить указатель на лог событий (для передачи в подсистемы)
   */
  [[nodiscard]] TelemetryEventLog* GetEventLog() { return &event_log_; }

 private:
  // PSRAM кольцевой буфер телеметрии
  TelemetryLog telem_log_;

  // Буфер событий (старт/стоп режимов и калибровок)
  TelemetryEventLog event_log_;

  // Время последней записи в лог
  uint32_t last_log_ms_{0};
};

}  // namespace rc_vehicle