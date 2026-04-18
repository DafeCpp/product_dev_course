#pragma once

#include <cstdint>

#include "mag_sensor.hpp"

#ifdef ESP_PLATFORM
#include "driver/i2c_master.h"

/**
 * Драйвер MMC5983MA по I2C (фиксированный адрес 0x30).
 *
 * Использует ESP-IDF v5.x новый I2C master API (i2c_master.h).
 * Логика инициализации и чтения идентична SPI-версии:
 *   - SW reset → Product ID → SET → CMM@100Hz
 *   - Периодическое SET/RESET каждые kSetResetPeriod чтений
 *   - 18-битные значения из 7-байтового burst read
 */
class Mmc5983I2c : public IMagSensor {
 public:
  struct Config {
    i2c_port_num_t port{I2C_NUM_0};
    gpio_num_t     sda_pin;
    gpio_num_t     scl_pin;
    uint32_t       speed_hz{400000};
  };

  /** Инициализация: создаёт I2C-шину и устройство, затем настраивает датчик. */
  int Init(const Config& cfg) noexcept;

  // IMagSensor
  int Init() override { return initialized_ ? 0 : -1; }
  int Read(MagData& data) override;
  int GetLastProductId() const override { return last_product_id_; }

 private:
  i2c_master_bus_handle_t bus_handle_{nullptr};
  i2c_master_dev_handle_t dev_handle_{nullptr};

  bool initialized_{false};
  int  last_product_id_{-1};
  uint32_t read_count_{0};

  int WriteReg(uint8_t reg, uint8_t value) noexcept;
  int ReadRegs(uint8_t reg, uint8_t* buf, size_t len) noexcept;
  int DoSet()   noexcept;
  int DoReset() noexcept;

  static constexpr uint8_t  kAddr          = 0x30;
  static constexpr uint32_t kSetResetPeriod = 100;
};

#endif  // ESP_PLATFORM
