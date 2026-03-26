#pragma once

#include <cstdint>

/** Данные IMU: акселерометр (g), гироскоп (dps). */
struct ImuData {
  float ax{0.f}, ay{0.f}, az{0.f};
  float gx{0.f}, gy{0.f}, gz{0.f};
};

/**
 * Абстрактный интерфейс IMU-датчика.
 * Реализован Mpu6050Spi и Lsm6ds3Spi.
 */
class IImuSensor {
 public:
  virtual ~IImuSensor() = default;

  /** Инициализация датчика. 0 — успех, -1 — ошибка. */
  virtual int Init() = 0;

  /** Чтение данных. 0 — успех, -1 — ошибка. */
  virtual int Read(ImuData& data) = 0;

  /** Последнее значение WHO_AM_I (-1 = не читали). */
  virtual int GetLastWhoAmI() const = 0;
};

/** Конвертация ImuData в формат телеметрии (mg, mdps → int16). */
inline void ImuDataConvertToTelem(const ImuData& data, int16_t& ax, int16_t& ay,
                                   int16_t& az, int16_t& gx, int16_t& gy,
                                   int16_t& gz) {
  ax = static_cast<int16_t>(data.ax * 1000.f);
  ay = static_cast<int16_t>(data.ay * 1000.f);
  az = static_cast<int16_t>(data.az * 1000.f);
  gx = static_cast<int16_t>(data.gx * 1000.f);
  gy = static_cast<int16_t>(data.gy * 1000.f);
  gz = static_cast<int16_t>(data.gz * 1000.f);
}
