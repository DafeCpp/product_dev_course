#pragma once

#include <cstdint>

#include "imu_sensor.hpp"
#include "spi_base.hpp"

/**
 * Драйвер MPU-6050 по SPI.
 * Использует `SpiDevice` для обмена; логика регистров и масштабирование — здесь.
 */
class Mpu6050Spi : public IImuSensor {
 public:
  explicit Mpu6050Spi(SpiDevice *spi) : spi_(spi) {}

  /** Инициализация: проверка WHO_AM_I, сброс SLEEP. 0 — успех, -1 — ошибка. */
  int Init() override;

  /** Чтение акселерометра и гироскопа в data. 0 — успех, -1 — ошибка. */
  int Read(ImuData &data) override;

  /** Конвертация в формат телеметрии (mg, mdps → int16). */
  static void ConvertToTelem(const ImuData &data, int16_t &ax, int16_t &ay,
                             int16_t &az, int16_t &gx, int16_t &gy, int16_t &gz);

  /** Для отладки: последнее прочитанное WHO_AM_I (0x68/0x70 = OK, -1 = не читали). */
  int GetLastWhoAmI() const override { return last_who_am_i_; }

 private:
  SpiDevice *spi_;
  bool initialized_{false};
  int last_who_am_i_{-1};

  int ReadReg(uint8_t reg, uint8_t &value);
  int WriteReg(uint8_t reg, uint8_t value);
  int ReadReg16(uint8_t reg, int16_t &value);
};
