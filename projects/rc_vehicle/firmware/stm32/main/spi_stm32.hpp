#pragma once

#include "spi_base.hpp"

/**
 * SPI bus for STM32 (STM32Cube LL).
 * Пины и периферия задаются в `board_pins.hpp`.
 */
class SpiBusStm32 : public SpiBus {
 public:
  int Init() override;

 private:
  bool inited_{false};
};

/** SPI device on an already configured `SpiBusStm32` (implements `SpiDevice`). */
class SpiDeviceStm32 : public SpiDevice {
 public:
  /** Устройство с дефолтным CS из `board_pins.hpp` (SPI_NCS_PORT/SPI_NCS_PIN). */
  explicit SpiDeviceStm32(SpiBusStm32 &bus);

  SpiDeviceStm32(SpiBusStm32 &bus, void *cs_port, unsigned int cs_pin);

  int Init() override;
  int Transfer(std::span<const uint8_t> tx, std::span<uint8_t> rx) override;

 private:
  SpiBusStm32 &bus_;
  void *cs_port_;
  uint32_t cs_pin_mask_;
  bool inited_{false};

  void CsLow();
  void CsHigh();
};
