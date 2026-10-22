#pragma once

#include <cstdint>
#include <span>

/** Данные IMU: акселерометр (g), гироскоп (dps). */
struct ImuData {
  float ax{0.f}, ay{0.f}, az{0.f};
  float gx{0.f}, gy{0.f}, gz{0.f};
};

/**
 * Привести показания физически установленного IMU к СК автомобиля.
 *
 * LOS-222: на текущей плате продольная ось датчика смотрит назад, а знак
 * gyro Z противоположен принятой в прошивке конвенции yaw_deg. После этой
 * нормализации X направлен вперёд, а положительный yaw_rate соответствует
 * левому повороту / росту yaw_deg.
 */
inline void NormalizeMountedImuToVehicleFrame(ImuData& data) {
  data.ax = -data.ax;
  data.gz = -data.gz;
}

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

  /**
   * Перечитать WHO_AM_I и сравнить со значением, найденным в Init() (LOS-383).
   * 0 — совпадает, -1 — ошибка SPI, несовпадение или датчик не
   * инициализирован.
   */
  virtual int CheckWhoAmI() = 0;
};

/**
 * Признак «пустой шины» в burst-ответе IMU (LOS-383): все байты данных равны
 * 0x00 (MISO прижат к земле / датчик без питания) или 0xFF (MISO висит в
 * воздухе). Живой датчик так ответить не может — гравитация всегда даёт
 * ненулевой акселерометр, а 0xFFFF по всем осям означает −1 LSB везде.
 */
inline bool IsStuckBusFrame(std::span<const uint8_t> data) {
  if (data.empty()) return false;
  const uint8_t first = data.front();
  if (first != 0x00 && first != 0xFF) return false;
  for (const uint8_t b : data) {
    if (b != first) return false;
  }
  return true;
}

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
