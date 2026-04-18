#pragma once

#include <cstddef>
#include <cstdint>

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
  bool GetLogFrame(size_t idx, TelemetryLogFrame& out) const {
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

 private:
  // PSRAM кольцевой буфер телеметрии
  TelemetryLog telem_log_;

  // Время последней записи в лог
  uint32_t last_log_ms_{0};
};

}  // namespace rc_vehicle