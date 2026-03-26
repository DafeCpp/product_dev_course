#include "imu.hpp"

#include "config.hpp"
#include "lsm6ds3_spi.hpp"
#include "mpu6050_spi.hpp"
#include "spi_esp32.hpp"

#ifdef ESP_PLATFORM
#include "esp_log.h"
static const char *IMU_TAG = "imu";
#endif

static SpiBusEsp32 g_spi_bus(IMU_SPI_HOST, IMU_SPI_SCK_PIN, IMU_SPI_MOSI_PIN,
                              IMU_SPI_MISO_PIN);
static SpiDeviceEsp32 g_spi_imu(g_spi_bus, IMU_SPI_CS_PIN, IMU_SPI_BAUD_HZ);

static Lsm6ds3Spi g_lsm(&g_spi_imu);
static Mpu6050Spi g_mpu(&g_spi_imu);

static IImuSensor *g_imu = nullptr;

int ImuInit(void) {
  // Пробуем LSM6DS3/LSM6DSL первым
  if (g_lsm.Init() == 0) {
    g_imu = &g_lsm;
#ifdef ESP_PLATFORM
    ESP_LOGI(IMU_TAG, "IMU: LSM6DS3 обнаружен (WHO_AM_I=0x%02X)",
             g_lsm.GetLastWhoAmI());
#endif
    return 0;
  }

  // Пробуем MPU-6050/MPU-6500
  if (g_mpu.Init() == 0) {
    g_imu = &g_mpu;
#ifdef ESP_PLATFORM
    ESP_LOGI(IMU_TAG, "IMU: MPU6050 обнаружен (WHO_AM_I=0x%02X)",
             g_mpu.GetLastWhoAmI());
#endif
    return 0;
  }

#ifdef ESP_PLATFORM
  ESP_LOGE(IMU_TAG, "IMU: датчик не обнаружен");
#endif
  return -1;
}

int ImuRead(ImuData &data) {
  if (!g_imu)
    return -1;
  return g_imu->Read(data);
}

void ImuConvertToTelem(const ImuData &data, int16_t &ax, int16_t &ay,
                       int16_t &az, int16_t &gx, int16_t &gy, int16_t &gz) {
  ImuDataConvertToTelem(data, ax, ay, az, gx, gy, gz);
}

int ImuGetLastWhoAmI(void) {
  return g_imu ? g_imu->GetLastWhoAmI() : -1;
}

const char *ImuGetSensorName(void) {
  if (!g_imu)
    return "none";
  int who = g_imu->GetLastWhoAmI();
  switch (who) {
    case 0x6A: return "LSM6DS3";
    case 0x6C: return "LSM6DSL";
    case 0x68: return "MPU6050";
    case 0x70: return "MPU6500";
    default:   return "unknown";
  }
}
