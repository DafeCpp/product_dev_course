#include "spi_pico.hpp"

#include "config.hpp"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

SpiPico::SpiPico(void *spi_id, unsigned int cs_pin, unsigned int sck_pin,
                 unsigned int mosi_pin, unsigned int miso_pin,
                 unsigned int baud_hz)
    : spi_id_(spi_id),
      cs_pin_(cs_pin),
      sck_pin_(sck_pin),
      mosi_pin_(mosi_pin),
      miso_pin_(miso_pin),
      baud_hz_(baud_hz) {}

void SpiPico::CsSelect() {
  gpio_put(static_cast<uint>(cs_pin_), 0);
}

void SpiPico::CsDeselect() {
  gpio_put(static_cast<uint>(cs_pin_), 1);
}

int SpiPico::Init() {
  gpio_init(static_cast<uint>(cs_pin_));
  gpio_set_dir(static_cast<uint>(cs_pin_), GPIO_OUT);
  gpio_put(static_cast<uint>(cs_pin_), 1);

  spi_init(static_cast<spi_inst_t *>(spi_id_), static_cast<uint32_t>(baud_hz_));
  gpio_set_function(static_cast<uint>(sck_pin_), GPIO_FUNC_SPI);
  gpio_set_function(static_cast<uint>(mosi_pin_), GPIO_FUNC_SPI);
  gpio_set_function(static_cast<uint>(miso_pin_), GPIO_FUNC_SPI);

  return 0;
}

int SpiPico::Transfer(const uint8_t *tx, uint8_t *rx, size_t len) {
  CsSelect();
  spi_write_read_blocking(static_cast<spi_inst_t *>(spi_id_), tx, rx, len);
  CsDeselect();
  return 0;
}
