#include "imu.hpp"

#include "config.hpp"
#include "hardware/spi.h"
#include "mpu6050_spi.hpp"
#include "spi_pico.hpp"

static SpiPico g_spi(static_cast<void *>(SPI_ID), SPI_CS_PIN, SPI_SCK_PIN,
                     SPI_MOSI_PIN, SPI_MISO_PIN, SPI_BAUD_HZ);
static Mpu6050Spi g_mpu(&g_spi);

int ImuInit(void) {
  return g_mpu.Init();
}

int ImuRead(ImuData &data) {
  return g_mpu.Read(data);
}

void ImuConvertToTelem(const ImuData &data, int16_t &ax, int16_t &ay,
                       int16_t &az, int16_t &gx, int16_t &gy, int16_t &gz) {
  Mpu6050Spi::ConvertToTelem(data, ax, ay, az, gx, gy, gz);
}

int ImuGetLastWhoAmI(void) {
  return g_mpu.GetLastWhoAmI();
}
