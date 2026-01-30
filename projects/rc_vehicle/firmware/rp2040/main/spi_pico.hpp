#pragma once

#include "spi_base.hpp"

/** Реализация SpiBase для Raspberry Pi Pico (Pico SDK). */
class SpiPico : public SpiBase {
 public:
  SpiPico(void *spi_id, unsigned int cs_pin, unsigned int sck_pin,
          unsigned int mosi_pin, unsigned int miso_pin, unsigned int baud_hz);

  int Init() override;
  int Transfer(const uint8_t *tx, uint8_t *rx, size_t len) override;

 private:
  void *spi_id_;
  unsigned int cs_pin_;
  unsigned int sck_pin_;
  unsigned int mosi_pin_;
  unsigned int miso_pin_;
  unsigned int baud_hz_;

  void CsSelect();
  void CsDeselect();
};
