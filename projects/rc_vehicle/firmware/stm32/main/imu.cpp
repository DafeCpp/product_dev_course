#include "imu.hpp"

#include "mpu6050_spi.hpp"
#include "spi_stm32.hpp"

static SpiStm32 g_spi;
static Mpu6050Spi g_mpu(&g_spi);

int ImuInit() { return g_mpu.Init(); }

int ImuRead(ImuData &data) { return g_mpu.Read(data); }

void ImuConvertToTelem(const ImuData &data, int16_t &ax, int16_t &ay,
                       int16_t &az, int16_t &gx, int16_t &gy, int16_t &gz) {
  Mpu6050Spi::ConvertToTelem(data, ax, ay, az, gx, gy, gz);
}
