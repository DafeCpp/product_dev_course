#include "mag.hpp"

#include "config.hpp"
#include "mmc5983_spi.hpp"
#include "spi_esp32.hpp"

#ifdef ESP_PLATFORM
#include "esp_log.h"
static const char* MAG_TAG = "mag";
#endif

// Магнитометр на отдельном CS, разделяет шину SPI2 с IMU
static SpiBusEsp32 g_mag_spi_bus(MAG_SPI_HOST, MAG_SPI_SCK_PIN, MAG_SPI_MOSI_PIN,
                                  MAG_SPI_MISO_PIN);
static SpiDeviceEsp32 g_spi_mag(g_mag_spi_bus, MAG_SPI_CS_PIN, MAG_SPI_BAUD_HZ);

static Mmc5983Spi g_mmc(&g_spi_mag);
static IMagSensor* g_mag = nullptr;

int MagInit(void) {
  if (g_mmc.Init() == 0) {
    g_mag = &g_mmc;
#ifdef ESP_PLATFORM
    ESP_LOGI(MAG_TAG, "Magnetometer: MMC5983MA обнаружен (Product ID=0x%02X)",
             g_mmc.GetLastProductId());
#endif
    return 0;
  }

#ifdef ESP_PLATFORM
  ESP_LOGE(MAG_TAG, "Magnetometer: датчик не обнаружен");
#endif
  return -1;
}

int MagRead(MagData& data) {
  if (!g_mag)
    return -1;
  return g_mag->Read(data);
}

int MagGetLastProductId(void) {
  return g_mag ? g_mag->GetLastProductId() : -1;
}

const char* MagGetSensorName(void) {
  if (!g_mag)
    return "none";
  return g_mag->GetLastProductId() == 0x30 ? "MMC5983MA" : "unknown";
}
