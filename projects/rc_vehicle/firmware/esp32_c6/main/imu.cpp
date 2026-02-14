#include "imu.hpp"

#include "config.hpp"
#include "mpu6050_spi.hpp"
#include "spi_esp32.hpp"

static SpiBusEsp32 g_spi_bus(IMU_SPI_HOST, IMU_SPI_SCK_PIN, IMU_SPI_MOSI_PIN,
                             IMU_SPI_MISO_PIN);
static SpiDeviceEsp32 g_spi_imu(g_spi_bus, IMU_SPI_CS_PIN, IMU_SPI_BAUD_HZ);
static Mpu6050Spi g_mpu(&g_spi_imu);

int ImuInit(void) { return g_mpu.Init(); }

int ImuRead(ImuData& data) { return g_mpu.Read(data); }

void ImuConvertToTelem(const ImuData& data, int16_t& ax, int16_t& ay,
                       int16_t& az, int16_t& gx, int16_t& gy, int16_t& gz) {
  Mpu6050Spi::ConvertToTelem(data, ax, ay, az, gx, gy, gz);
}

int ImuGetLastWhoAmI(void) { return g_mpu.GetLastWhoAmI(); }
