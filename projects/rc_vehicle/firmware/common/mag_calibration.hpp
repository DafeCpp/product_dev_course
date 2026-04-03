#pragma once

#include "mag_sensor.hpp"

/**
 * @brief Данные калибровки магнитометра.
 *
 * Содержит:
 *  - hard iron offset (центр окружности)
 *  - normal: нормаль к плоскости вращения = ось «вверх» в СК датчика
 *  - basis1, basis2: ортобазис в горизонтальной плоскости для вычисления heading
 */
struct MagCalibData {
  float offset[3]{0.f, 0.f, 0.f};  ///< Hard iron offset [мГс] (X, Y, Z)
  float normal[3]{0.f, 0.f, 1.f};  ///< Нормаль к плоскости вращения (единичный вектор)
  float basis1[3]{1.f, 0.f, 0.f};  ///< Ортобазис 1 в горизонтальной плоскости
  float basis2[3]{0.f, 1.f, 0.f};  ///< Ортобазис 2 в горизонтальной плоскости
  bool valid{false};
};

/**
 * @brief Статус калибровки магнитометра.
 */
enum class MagCalibStatus { Idle, Collecting, Done, Failed };

/**
 * @brief Причина неудачи калибровки магнитометра.
 */
enum class MagCalibFailReason {
  None,           ///< Нет ошибки (или калибровка ещё не завершена)
  TooFewSamples,  ///< Мало семплов — нажали Finish слишком быстро
  RadiusTooSmall, ///< Мало вращения — min/max по осям почти совпали
  RadiusTooLarge, ///< Сильные помехи — аномально большой разброс
  NotPlanar,      ///< Данные не образуют плоскость — вращение не в одной оси
};

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

  /** Причина неудачи (валидна только при status == Failed). */
  [[nodiscard]] MagCalibFailReason GetFailReason() const noexcept {
    return fail_reason_;
  }

  /** Строковое описание причины неудачи (для UI/логов). */
  [[nodiscard]] const char* GetFailReasonStr() const noexcept;


  // ─── Валидационные границы ───────────────────────────────────────────────

  /** Минимальный средний радиус сферы [мГс]. Меньше → недостаточное вращение. */
  static constexpr float kMinRadius = 20.f;

  /** Максимальный средний радиус сферы [мГс]. Больше → сильные помехи. */
  static constexpr float kMaxRadius = 1500.f;

  /** Минимальное количество семплов для попытки завершения. */
  static constexpr int kMinSamples = 200;

  /** Порог планарности: λ_min / λ_mid < threshold → данные в плоскости. */
  static constexpr float kPlanarityThreshold = 0.3f;

 private:
  /** Якоби-итерация для 3×3 симметричной матрицы → собственные значения/вектора. */
  static void Jacobi3x3(float A[3][3], float V[3][3], int max_iter = 30);

  /** Вычислить ортобазис (basis1, basis2) в плоскости, перпендикулярной normal. */
  static void ComputeBasis(const float normal[3], float basis1[3], float basis2[3]);

  MagCalibStatus status_{MagCalibStatus::Idle};
  MagCalibFailReason fail_reason_{MagCalibFailReason::None};
  MagCalibData data_{};
  float min_[3]{};
  float max_[3]{};
  int sample_count_{0};

  // Онлайн-накопление ковариации для PCA
  float sum_[3]{};       ///< Σ(x, y, z)
  float cov_sum_[6]{};   ///< Σ(xx, xy, xz, yy, yz, zz) — верхний треугольник
};
