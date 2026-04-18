#include "mag.hpp"

#include "config.hpp"

#ifdef ESP_PLATFORM
#include "esp_log.h"
static const char* MAG_TAG = "mag";
#endif

// ─── Выбор драйвера: I2C или SPI ─────────────────────────────────────────

#ifdef MAG_USE_I2C

#include "mmc5983_i2c.hpp"
static Mmc5983I2c g_mmc;

int MagInit(void) {
  Mmc5983I2c::Config cfg{
      .port    = MAG_I2C_PORT,
      .sda_pin = MAG_I2C_SDA_PIN,
      .scl_pin = MAG_I2C_SCL_PIN,
      .speed_hz = MAG_I2C_BAUD_HZ,
  };
  if (g_mmc.Init(cfg) == 0) {
#ifdef ESP_PLATFORM
    ESP_LOGI(MAG_TAG, "Magnetometer: MMC5983MA (I2C, addr=0x30, Product ID=0x%02X)",
             g_mmc.GetLastProductId());
#endif
    return 0;
  }
#ifdef ESP_PLATFORM
  ESP_LOGE(MAG_TAG, "Magnetometer: MMC5983MA (I2C) не обнаружен");
#endif
  return -1;
}

#else  // SPI

#include "mmc5983_spi.hpp"
#include "spi_esp32.hpp"

static SpiBusEsp32    g_mag_spi_bus(MAG_SPI_HOST, MAG_SPI_SCK_PIN,
                                    MAG_SPI_MOSI_PIN, MAG_SPI_MISO_PIN);
static SpiDeviceEsp32 g_spi_mag(g_mag_spi_bus, MAG_SPI_CS_PIN, MAG_SPI_BAUD_HZ);
static Mmc5983Spi     g_mmc(&g_spi_mag);

int MagInit(void) {
  if (g_mmc.Init() == 0) {
#ifdef ESP_PLATFORM
    ESP_LOGI(MAG_TAG, "Magnetometer: MMC5983MA (SPI, Product ID=0x%02X)",
             g_mmc.GetLastProductId());
#endif
    return 0;
  }
#ifdef ESP_PLATFORM
  ESP_LOGE(MAG_TAG, "Magnetometer: MMC5983MA (SPI) не обнаружен");
#endif
  return -1;
}

#endif  // MAG_USE_I2C

// ─── Общие функции (не зависят от интерфейса) ────────────────────────────

int MagRead(MagData& data) {
  return g_mmc.Read(data);
}

int MagGetLastProductId(void) {
  return g_mmc.GetLastProductId();
}

const char* MagGetSensorName(void) {
  return g_mmc.GetLastProductId() == 0x30 ? "MMC5983MA" : "none";
}
