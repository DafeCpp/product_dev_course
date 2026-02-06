#pragma once

#include "spi_base.hpp"

/** SPI bus implementation for Raspberry Pi Pico (Pico SDK). */
class SpiBusPico : public SpiBus {
 public:
  SpiBusPico(void *spi_id, unsigned int sck_pin, unsigned int mosi_pin,
             unsigned int miso_pin, unsigned int baud_hz);

  int Init() override;

  void *Id() const { return spi_id_; }

 private:
  void *spi_id_;
  unsigned int sck_pin_;
  unsigned int mosi_pin_;
  unsigned int miso_pin_;
  unsigned int baud_hz_;

  bool inited_{false};
};

/** SPI device on a `SpiBusPico` (implements `SpiDevice`). */
class SpiDevicePico : public SpiDevice {
 public:
  SpiDevicePico(SpiBusPico &bus, unsigned int cs_pin);

  int Init() override;
  int Transfer(std::span<const uint8_t> tx, std::span<uint8_t> rx) override;

 private:
  SpiBusPico &bus_;
  unsigned int cs_pin_;
  bool inited_{false};

  void CsSelect();
  void CsDeselect();
};
