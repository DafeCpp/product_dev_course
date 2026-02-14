#pragma once

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "spi_base.hpp"

/**
 * SPI bus (ESP-IDF SPI master).
 *
 * Отдельный класс для шины нужен, чтобы на одном SPI host можно было
 * зарегистрировать несколько устройств (с разными CS/частотой/режимом),
 * не дублируя конфигурацию пинов шины.
 */
class SpiBusEsp32 : public SpiBus {
 public:
  SpiBusEsp32(spi_host_device_t host, gpio_num_t sck_pin, gpio_num_t mosi_pin,
              gpio_num_t miso_pin, int max_transfer_sz = 64,
              spi_dma_chan_t dma_chan = SPI_DMA_CH_AUTO);

  int Init() override;

  spi_host_device_t Host() const { return host_; }

 private:
  spi_host_device_t host_;
  gpio_num_t sck_pin_;
  gpio_num_t mosi_pin_;
  gpio_num_t miso_pin_;
  int max_transfer_sz_;
  spi_dma_chan_t dma_chan_;

  bool inited_{false};
};

/** SPI device on an already configured `SpiBusEsp32` (implements `SpiDevice`). */
class SpiDeviceEsp32 : public SpiDevice {
 public:
  SpiDeviceEsp32(SpiBusEsp32& bus, gpio_num_t cs_pin, int clock_hz, int mode = 0,
                 int queue_size = 1);

  int Init() override;
  int Transfer(std::span<const uint8_t> tx, std::span<uint8_t> rx) override;

 private:
  SpiBusEsp32& bus_;
  gpio_num_t cs_pin_;
  int clock_hz_;
  int mode_;
  int queue_size_;

  spi_device_handle_t dev_{nullptr};
  bool inited_{false};
};

