#pragma once

#include <cstddef>
#include <cstdint>

#include "telemetry_config_snapshot.hpp"
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
  TelemetryManager() { config_snapshots_.SetFrameLog(&telem_log_); }
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
  [[nodiscard]] bool BeginLogExport(size_t& count_out) {
    return telem_log_.BeginExport(count_out);
  }
  [[nodiscard]] bool BeginLogAndConfigExport(size_t& frame_count_out,
                                             TelemetryLogFrame& tail_out,
                                             size_t& snapshot_count_out) {
    return config_snapshots_.BeginExportWithFrames(
        telem_log_, frame_count_out, tail_out, snapshot_count_out);
  }
  [[nodiscard]] bool FinalizeConfigSnapshotExport(size_t& snapshot_count_out) {
    return config_snapshots_.FinalizeExportWithFrameBoundary(
        snapshot_count_out);
  }
  [[nodiscard]] size_t CopyLogExportFrames(size_t start_idx,
                                           TelemetryLogFrame* out,
                                           size_t max_count) const {
    return telem_log_.CopyExportFrames(start_idx, out, max_count);
  }
  void EndLogExport() { telem_log_.EndExport(); }

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

  void PushConfigSnapshot(uint32_t ts_ms, const StabilizationConfig& cfg) {
    config_snapshots_.Push(TelemetryConfigSnapshot::FromConfig(ts_ms, cfg));
  }
  [[nodiscard]] size_t GetConfigSnapshotCount() const {
    return config_snapshots_.Count();
  }
  [[nodiscard]] bool GetConfigSnapshot(size_t idx,
                                       TelemetryConfigSnapshot& out) const {
    return config_snapshots_.GetSnapshot(idx, out);
  }
  [[nodiscard]] size_t CopyConfigSnapshots(TelemetryConfigSnapshot* out,
                                           size_t max_count) const {
    return config_snapshots_.CopySnapshots(out, max_count);
  }
  [[nodiscard]] bool BeginConfigSnapshotExport(uint32_t max_ts_ms,
                                               size_t& count_out) {
    return config_snapshots_.BeginExport(max_ts_ms, count_out);
  }
  [[nodiscard]] bool GetNextConfigSnapshotExport(TelemetryConfigSnapshot& out) {
    return config_snapshots_.GetNextExportSnapshot(out);
  }
  void EndConfigSnapshotExport() { config_snapshots_.EndExport(); }
  void ClearConfigSnapshots() { config_snapshots_.Clear(); }
  [[nodiscard]] TelemetryConfigSnapshotLog* GetConfigSnapshotLog() {
    return &config_snapshots_;
  }

  /**
   * @brief Получить указатель на лог событий (для передачи в подсистемы)
   */
  [[nodiscard]] TelemetryEventLog* GetEventLog() { return &event_log_; }

 private:
  // PSRAM кольцевой буфер телеметрии
  TelemetryLog telem_log_;

  // Буфер событий (старт/стоп режимов и калибровок)
  TelemetryEventLog event_log_;
  TelemetryConfigSnapshotLog config_snapshots_;

  // Время последней записи в лог
  uint32_t last_log_ms_{0};
};

}  // namespace rc_vehicle
