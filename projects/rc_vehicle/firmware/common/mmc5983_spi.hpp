#pragma once

#include <cstdint>

#include "mag_sensor.hpp"
#include "spi_base.hpp"

/**
 * Драйвер MMC5983MA по SPI (Mode 3: CPOL=1, CPHA=1).
 *
 * Особенности:
 * - 18-битный вывод X/Y/Z: старшие 16 бит в регистрах 0x00-0x05,
 *   младшие 2 бита каждой оси — в регистре 0x06.
 * - Continuous Measurement Mode (CMM) на 100 Гц.
 * - Автоматическое чередование SET/RESET каждые kSetResetPeriod измерений
 *   для компенсации temperature offset и подавления drift.
 * - Product ID (WHO_AM_I): регистр 0x2F, ожидаемое значение 0x30.
 */
class Mmc5983Spi : public IMagSensor {
 public:
  explicit Mmc5983Spi(SpiDevice* spi) : spi_(spi) {}

  /** Инициализация: SW reset → проверка Product ID → SET → запуск CMM. */
  int Init() override;

  /** Бёрст-чтение 7 байт (регистры 0x00-0x06), сборка 18-bit значений. */
  int Read(MagData& data) override;

  /** Последнее прочитанное Product ID (0x30 = OK, -1 = не читали). */
  int GetLastProductId() const override { return last_product_id_; }

 private:
  SpiDevice* spi_;
  bool initialized_{false};
  int last_product_id_{-1};
  uint32_t read_count_{0};

  // Период чередования SET/RESET (в количестве вызовов Read).
  // 100 измерений = ~1 сек при 100 Гц.
  static constexpr uint32_t kSetResetPeriod = 100;

  int ReadReg(uint8_t reg, uint8_t& value);
  int WriteReg(uint8_t reg, uint8_t value);

  /** Подать SET-импульс (намагничивание в прямом направлении). */
  int DoSet();
  /** Подать RESET-импульс (намагничивание в обратном направлении). */
  int DoReset();
};
