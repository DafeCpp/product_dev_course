#pragma once

#include <cstdint>

#include "mpu6050_spi.hpp"  // ImuData

/** Калибровочные данные IMU: bias гироскопа и акселерометра. */
struct ImuCalibData {
  float gyro_bias[3]{0.f, 0.f, 0.f};   // gx, gy, gz offset (dps)
  float accel_bias[3]{0.f, 0.f, 0.f};  // ax, ay, az offset (g)
  bool valid{false};
};

/** Режим калибровки. */
enum class CalibMode {
  GyroOnly,  // только гироскоп (быстро, ~2 сек)
  Full,      // гироскоп + акселерометр
};

/** Состояние процесса калибровки. */
enum class CalibStatus {
  Idle,        // калибровка не запущена
  Collecting,  // идёт сбор семплов
  Done,        // калибровка завершена успешно
  Failed,      // калибровка не удалась (движение обнаружено)
};

/**
 * Калибровка IMU: сбор bias гироскопа/акселерометра, применение компенсации.
 *
 * Использование:
 *   1. (опционально) SetData() — загрузить сохранённые данные из NVS
 *   2. StartCalibration(mode) — запустить авто-калибровку
 *   3. В control loop: FeedSample(raw) на каждом семпле (500 Гц)
 *   4. Когда GetStatus() == Done — калибровка завершена
 *   5. Apply(data) — вычесть bias из сырых данных перед обработкой
 *
 * Платформонезависимый: не зависит от ESP-IDF, FreeRTOS и т.д.
 */
class ImuCalibration {
 public:
  /** Запустить процесс калибровки. num_samples — количество семплов для сбора. */
  void StartCalibration(CalibMode mode, int num_samples = 1000);

  /** Подать очередной семпл (вызывать каждую итерацию control loop при Collecting). */
  void FeedSample(const ImuData& raw);

  /** Применить компенсацию bias к данным (вычитание). */
  void Apply(ImuData& data) const;

  /** Текущий статус калибровки. */
  CalibStatus GetStatus() const { return status_; }

  /** Получить текущие калибровочные данные. */
  const ImuCalibData& GetData() const { return data_; }

  /** Загрузить калибровочные данные (из NVS или внешнего источника). */
  void SetData(const ImuCalibData& data);

  /** Калибровка валидна и можно применять Apply(). */
  bool IsValid() const { return data_.valid; }

  // Пороги для детекции движения (variance по оси)
  static constexpr float kGyroVarianceThreshold = 0.5f;    // (dps)^2
  static constexpr float kAccelVarianceThreshold = 0.01f;   // (g)^2

  // Максимально допустимый bias (для валидации данных из NVS)
  static constexpr float kMaxGyroBias = 20.0f;   // dps
  static constexpr float kMaxAccelBias = 0.5f;    // g

 private:
  ImuCalibData data_{};
  CalibStatus status_{CalibStatus::Idle};
  CalibMode mode_{CalibMode::GyroOnly};

  // Аккумуляторы для вычисления mean и variance (Welford's algorithm)
  int target_samples_{0};
  int collected_{0};
  double sum_[6]{};      // gx, gy, gz, ax, ay, az
  double sum_sq_[6]{};   // сумма квадратов для variance

  void ResetAccumulators();
  bool Finalize();
};
