#include "spi_pico.hpp"

#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

SpiBusPico::SpiBusPico(void *spi_id, unsigned int sck_pin, unsigned int mosi_pin,
                       unsigned int miso_pin, unsigned int baud_hz)
    : spi_id_(spi_id),
      sck_pin_(sck_pin),
      mosi_pin_(mosi_pin),
      miso_pin_(miso_pin),
      baud_hz_(baud_hz) {}

int SpiBusPico::Init() {
  if (inited_) return 0;

  spi_init(static_cast<spi_inst_t *>(spi_id_), static_cast<uint32_t>(baud_hz_));
  gpio_set_function(static_cast<uint>(sck_pin_), GPIO_FUNC_SPI);
  gpio_set_function(static_cast<uint>(mosi_pin_), GPIO_FUNC_SPI);
  gpio_set_function(static_cast<uint>(miso_pin_), GPIO_FUNC_SPI);

  inited_ = true;
  return 0;
}

SpiDevicePico::SpiDevicePico(SpiBusPico &bus, unsigned int cs_pin)
    : bus_(bus), cs_pin_(cs_pin) {}

void SpiDevicePico::CsSelect() {
  gpio_put(static_cast<uint>(cs_pin_), 0);
}

void SpiDevicePico::CsDeselect() {
  gpio_put(static_cast<uint>(cs_pin_), 1);
}

int SpiDevicePico::Init() {
  if (inited_) return 0;
  if (bus_.Init() != 0) return -1;

  gpio_init(static_cast<uint>(cs_pin_));
  gpio_set_dir(static_cast<uint>(cs_pin_), GPIO_OUT);
  gpio_put(static_cast<uint>(cs_pin_), 1);

  inited_ = true;
  return 0;
}

int SpiDevicePico::Transfer(std::span<const uint8_t> tx,
                            std::span<uint8_t> rx) {
  if (!inited_) return -1;
  if (tx.size() == 0 || tx.size() != rx.size()) return -1;
  CsSelect();
  spi_write_read_blocking(static_cast<spi_inst_t *>(bus_.Id()), tx.data(),
                          rx.data(), tx.size());
  CsDeselect();
  return 0;
}
