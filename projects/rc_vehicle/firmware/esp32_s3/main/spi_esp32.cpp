#include "spi_esp32.hpp"

#include <cstring>

#include "esp_err.h"
#include "esp_log.h"

static const std::string_view TAG = "spi_esp32";

SpiBusEsp32::SpiBusEsp32(spi_host_device_t host, gpio_num_t sck_pin,
                         gpio_num_t mosi_pin, gpio_num_t miso_pin,
                         int max_transfer_sz, spi_dma_chan_t dma_chan)
    : host_(host),
      sck_pin_(sck_pin),
      mosi_pin_(mosi_pin),
      miso_pin_(miso_pin),
      max_transfer_sz_(max_transfer_sz),
      dma_chan_(dma_chan) {}

int SpiBusEsp32::Init() {
  if (inited_) return 0;

  spi_bus_config_t buscfg = {};
  buscfg.miso_io_num = miso_pin_;
  buscfg.mosi_io_num = mosi_pin_;
  buscfg.sclk_io_num = sck_pin_;
  buscfg.quadwp_io_num = GPIO_NUM_NC;
  buscfg.quadhd_io_num = GPIO_NUM_NC;
  buscfg.max_transfer_sz = max_transfer_sz_;

  esp_err_t e = spi_bus_initialize(host_, &buscfg, dma_chan_);
  if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(e));
    return -1;
  }

  inited_ = true;
  ESP_LOGI(
      TAG,
      "SPI bus initialized (host=%d, sck=%d, mosi=%d, miso=%d, max_xfer=%d)",
      (int)host_, (int)sck_pin_, (int)mosi_pin_, (int)miso_pin_,
      max_transfer_sz_);
  return 0;
}

SpiDeviceEsp32::SpiDeviceEsp32(SpiBusEsp32& bus, gpio_num_t cs_pin,
                               int clock_hz, int mode, int queue_size)
    : bus_(bus),
      cs_pin_(cs_pin),
      clock_hz_(clock_hz),
      mode_(mode),
      queue_size_(queue_size) {}

int SpiDeviceEsp32::Init() {
  if (inited_) return 0;
  if (bus_.Init() != 0) return -1;

  spi_device_interface_config_t devcfg = {};
  devcfg.clock_speed_hz = clock_hz_;
  devcfg.mode = mode_;
  devcfg.spics_io_num = cs_pin_;
  devcfg.queue_size = queue_size_;

  esp_err_t e = spi_bus_add_device(bus_.Host(), &devcfg, &dev_);
  if (e != ESP_OK) {
    ESP_LOGE(TAG, "spi_bus_add_device failed: %s", esp_err_to_name(e));
    return -1;
  }

  inited_ = true;
  ESP_LOGI(TAG, "SPI device added (host=%d, cs=%d, mode=%d, %d Hz)",
           (int)bus_.Host(), (int)cs_pin_, mode_, clock_hz_);
  return 0;
}

int SpiDeviceEsp32::Transfer(std::span<const uint8_t> tx,
                             std::span<uint8_t> rx) {
  if (!inited_) return -1;
  if (tx.size() == 0 || tx.size() != rx.size()) return -1;

  spi_transaction_t t = {};
  t.length = tx.size() * 8;
  t.tx_buffer = tx.data();
  t.rx_buffer = rx.data();

  esp_err_t e = spi_device_transmit(dev_, &t);
  return (e == ESP_OK) ? 0 : -1;
}
