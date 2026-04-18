#pragma once

#include <cstdint>

#include "imu_sensor.hpp"
#include "spi_base.hpp"

/**
 * Драйвер LSM6DS3/LSM6DSL по SPI.
 * WHO_AM_I: 0x6A (LSM6DS3), 0x6C (LSM6DSL).
 * Реализует IImuSensor, совместим с инфраструктурой MPU-6050.
 */
class Lsm6ds3Spi : public IImuSensor {
 public:
  explicit Lsm6ds3Spi(SpiDevice *spi) : spi_(spi) {}

  /** Инициализация: проверка WHO_AM_I, настройка ODR/FS. 0 — успех, -1 — ошибка. */
  int Init() override;

  /** Бёрст-чтение акселерометра и гироскопа. 0 — успех, -1 — ошибка. */
  int Read(ImuData &data) override;

  /** Для отладки: последнее прочитанное WHO_AM_I (0x6A/0x6C = OK, -1 = не читали). */
  int GetLastWhoAmI() const override { return last_who_am_i_; }

 private:
  SpiDevice *spi_;
  bool initialized_{false};
  int last_who_am_i_{-1};

  int ReadReg(uint8_t reg, uint8_t &value);
  int WriteReg(uint8_t reg, uint8_t value);
};
