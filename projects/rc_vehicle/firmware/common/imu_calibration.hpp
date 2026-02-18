#pragma once

#include <cstdint>

#include "mpu6050_spi.hpp"  // ImuData

/** Калибровочные данные IMU: bias, вектор g при покое, направление «вперёд». */
struct ImuCalibData {
  float gyro_bias[3]{0.f, 0.f, 0.f};   // gx, gy, gz offset (dps)
  float accel_bias[3]{0.f, 0.f, 0.f};  // ax, ay, az offset (g)
  /** Единичный вектор g в СК датчика (этап 1: стояние на месте). */
  float gravity_vec[3]{0.f, 0.f, 1.f};
  /** Единичный вектор «вперёд» в СК датчика (этап 2: движение вперёд/назад). Продольное ускорение = dot(accel, vec). */
  float accel_forward_vec[3]{1.f, 0.f, 0.f};
  bool valid{false};
};

/** Режим калибровки. */
enum class CalibMode {
  GyroOnly,  // только гироскоп (быстро, ~2 сек)
  Full,      // этап 1: стояние на месте — gyro/accel bias + вектор g
  Forward,   // этап 2: движение вперёд/назад с прямыми колёсами — вектор «вперёд»
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
  /** Запустить этап 1 (Full) или GyroOnly. num_samples — количество семплов. */
  void StartCalibration(CalibMode mode, int num_samples = 1000);

  /** Запустить этап 2 (Forward): требует валидную калибровку с gravity_vec. num_samples — сбор при движении вперёд/назад. */
  bool StartForwardCalibration(int num_samples = 2000);

  /** Подать очередной семпл (вызывать каждую итерацию control loop при Collecting). */
  void FeedSample(const ImuData& raw);

  /** Текущий этап калибровки: 0 = нет, 1 = стояние на месте, 2 = движение вперёд/назад. */
  int GetCalibStage() const;

  /** Применить компенсацию bias к данным (вычитание). */
  void Apply(ImuData& data) const;

  /**
   * Продольное ускорение (вперёд/назад) в g.
   * Вызывать после Apply(data). Положительное = ускорение вперёд.
   * Считается как скалярное произведение (ax,ay,az) на единичный вектор направления.
   */
  float GetForwardAccel(const ImuData& data) const;

  /** Задать направление «вперёд» единичным вектором в СК датчика (fx,fy,fz). Нормализуется. */
  void SetForwardDirection(float fx, float fy, float fz);

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

  // Аккумуляторы этап 1 (Welford)
  int target_samples_{0};
  int collected_{0};
  double sum_[6]{};
  double sum_sq_[6]{};

  // Аккумуляторы этап 2 (линейное ускорение при движении)
  double sum_linear_[3]{};
  float first_linear_[3]{};
  bool first_linear_set_{false};

  static constexpr float kLinearAccelThreshold = 0.05f;  // (g) порог для учёта семпла

  void ResetAccumulators();
  bool Finalize();
  bool FinalizeForward();
};
