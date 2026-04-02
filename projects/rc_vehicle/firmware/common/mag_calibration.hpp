#pragma once

#include "mag_sensor.hpp"

/**
 * @brief Данные калибровки магнитометра (hard iron offset).
 */
struct MagCalibData {
  float offset[3]{0.f, 0.f, 0.f};  ///< Hard iron offset [мГс] (X, Y, Z)
  bool valid{false};
};

/**
 * @brief Статус калибровки магнитометра.
 */
enum class MagCalibStatus { Idle, Collecting, Done, Failed };

/**
 * @brief Hard iron калибровка магнитометра.
 *
 * Собирает min/max по каждой оси пока машина вращается,
 * вычисляет offset = (max + min) / 2.
 *
 * Использование:
 *   1. Start() — начать сбор семплов
 *   2. FeedSample() — подавать данные магнитометра в control loop
 *   3. Finish() — вычислить offset; статус становится Done или Failed
 *   4. Apply() — вычесть offset из сырых данных
 *   5. SetData() — восстановить калибровку из NVS
 */
class MagCalibration {
 public:
  /** Начать сбор семплов. */
  void Start();

  /** Обновить min/max по текущему семплу (вызывается в control loop). */
  void FeedSample(const MagData& m);

  /**
   * @brief Вычислить offset и завершить калибровку.
   *
   * Проверяет:
   *   - sample_count >= kMinSamples
   *   - средний радиус (среднее из (max[i]-min[i])/2) в [kMinRadius,
   * kMaxRadius]
   *
   * При успехе: status = Done, data_.valid = true.
   * При ошибке: status = Failed.
   */
  void Finish();

  /** Прервать сбор, вернуться в Idle. */
  void Cancel();

  /**
   * @brief Вычесть offset из данных (если калибровка валидна).
   * @param m Данные магнитометра, модифицируются in-place.
   */
  void Apply(MagData& m) const;

  /** Установить данные калибровки (например, загруженные из NVS). */
  void SetData(const MagCalibData& d);

  /** Получить текущие данные калибровки. */
  [[nodiscard]] const MagCalibData& GetData() const noexcept { return data_; }

  /** Текущий статус. */
  [[nodiscard]] MagCalibStatus GetStatus() const noexcept { return status_; }

  /** true если калибровка завершена и данные валидны. */
  [[nodiscard]] bool IsValid() const noexcept { return data_.valid; }

  /** true если идёт сбор семплов. */
  [[nodiscard]] bool IsCollecting() const noexcept {
    return status_ == MagCalibStatus::Collecting;
  }

  // ─── Валидационные границы ───────────────────────────────────────────────

  /** Минимальный средний радиус сферы [мГс]. Меньше → недостаточное вращение. */
  static constexpr float kMinRadius = 20.f;

  /** Максимальный средний радиус сферы [мГс]. Больше → сильные помехи. */
  static constexpr float kMaxRadius = 1500.f;

  /** Минимальное количество семплов для попытки завершения. */
  static constexpr int kMinSamples = 200;

 private:
  MagCalibStatus status_{MagCalibStatus::Idle};
  MagCalibData data_{};
  float min_[3]{};
  float max_[3]{};
  int sample_count_{0};
};
