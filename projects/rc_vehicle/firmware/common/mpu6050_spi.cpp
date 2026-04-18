#include "mpu6050_spi.hpp"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static const char* MPU_TAG = "mpu6050_spi";
#endif

// Регистры MPU-6050
#define MPU6050_REG_PWR_MGMT_1 0x6B
#define MPU6050_REG_ACCEL_XOUT_H 0x3B
#define MPU6050_REG_GYRO_XOUT_H 0x43
#define MPU6050_REG_WHO_AM_I 0x75

#define MPU6050_WHO_AM_I_VALUE 0x68
#define MPU6500_WHO_AM_I_VALUE 0x70  // MPU-6500 (совместим по регистрам с MPU-6050)
#define MPU6050_SPI_READ_BIT 0x80

#define MPU6050_ACCEL_SCALE (16384.0f)
#define MPU6050_GYRO_SCALE (131.0f)

int Mpu6050Spi::ReadReg(uint8_t reg, uint8_t &value) {
  uint8_t tx[2] = {static_cast<uint8_t>(reg | MPU6050_SPI_READ_BIT), 0};
  uint8_t rx[2] = {0, 0};
  if (spi_->Transfer(std::span<const uint8_t>(tx), std::span<uint8_t>(rx)) != 0)
    return -1;
  value = rx[1];
  return 0;
}

int Mpu6050Spi::WriteReg(uint8_t reg, uint8_t value) {
  uint8_t tx[2] = {reg, value};
  uint8_t rx[2] = {0, 0};
  return spi_->Transfer(std::span<const uint8_t>(tx), std::span<uint8_t>(rx)) ==
                 0
             ? 0
             : -1;
}

int Mpu6050Spi::ReadReg16(uint8_t reg, int16_t &value) {
  uint8_t tx[3] = {static_cast<uint8_t>(reg | MPU6050_SPI_READ_BIT), 0, 0};
  uint8_t rx[3] = {0, 0, 0};
  if (spi_->Transfer(std::span<const uint8_t>(tx), std::span<uint8_t>(rx)) != 0)
    return -1;
  value = static_cast<int16_t>((rx[1] << 8) | rx[2]);
  return 0;
}

int Mpu6050Spi::Init() {
  if (initialized_)
    return 0;
  if (spi_->Init() != 0)
    return -1;

  // Сброс датчика: записать 0x80 в PWR_MGMT_1 (DEVICE_RESET)
  (void)WriteReg(MPU6050_REG_PWR_MGMT_1, 0x80);

#ifdef ESP_PLATFORM
  vTaskDelay(pdMS_TO_TICKS(100));
#endif

  // Попытки чтения WHO_AM_I с задержками (датчик может быть не готов)
  uint8_t who_am_i = 0;
  constexpr int kMaxRetries = 5;
  for (int attempt = 0; attempt < kMaxRetries; ++attempt) {
    int rc = ReadReg(MPU6050_REG_WHO_AM_I, who_am_i);
#ifdef ESP_PLATFORM
    ESP_LOGI(MPU_TAG, "WHO_AM_I attempt %d: rc=%d, value=0x%02X", attempt, rc,
             who_am_i);
#endif
    if (rc == 0 &&
        (who_am_i == MPU6050_WHO_AM_I_VALUE ||
         who_am_i == MPU6500_WHO_AM_I_VALUE)) {
      break;
    }
#ifdef ESP_PLATFORM
    vTaskDelay(pdMS_TO_TICKS(50));
#endif
  }

  last_who_am_i_ = static_cast<int>(who_am_i);
  if (who_am_i != MPU6050_WHO_AM_I_VALUE && who_am_i != MPU6500_WHO_AM_I_VALUE)
    return -1;
  if (WriteReg(MPU6050_REG_PWR_MGMT_1, 0x00) != 0)
    return -1;

  initialized_ = true;
  return 0;
}

int Mpu6050Spi::Read(ImuData &data) {
  if (!initialized_)
    return -1;

  int16_t raw_ax, raw_ay, raw_az;
  int16_t raw_gx, raw_gy, raw_gz;

  if (ReadReg16(MPU6050_REG_ACCEL_XOUT_H, raw_ax) != 0)
    return -1;
  if (ReadReg16(MPU6050_REG_ACCEL_XOUT_H + 2, raw_ay) != 0)
    return -1;
  if (ReadReg16(MPU6050_REG_ACCEL_XOUT_H + 4, raw_az) != 0)
    return -1;
  if (ReadReg16(MPU6050_REG_GYRO_XOUT_H, raw_gx) != 0)
    return -1;
  if (ReadReg16(MPU6050_REG_GYRO_XOUT_H + 2, raw_gy) != 0)
    return -1;
  if (ReadReg16(MPU6050_REG_GYRO_XOUT_H + 4, raw_gz) != 0)
    return -1;

  data.ax = static_cast<float>(raw_ax) / MPU6050_ACCEL_SCALE;
  data.ay = static_cast<float>(raw_ay) / MPU6050_ACCEL_SCALE;
  data.az = static_cast<float>(raw_az) / MPU6050_ACCEL_SCALE;
  data.gx = static_cast<float>(raw_gx) / MPU6050_GYRO_SCALE;
  data.gy = static_cast<float>(raw_gy) / MPU6050_GYRO_SCALE;
  data.gz = static_cast<float>(raw_gz) / MPU6050_GYRO_SCALE;

  return 0;
}

void Mpu6050Spi::ConvertToTelem(const ImuData &data, int16_t &ax, int16_t &ay,
                                int16_t &az, int16_t &gx, int16_t &gy,
                                int16_t &gz) {
  ax = static_cast<int16_t>(data.ax * 1000.f);
  ay = static_cast<int16_t>(data.ay * 1000.f);
  az = static_cast<int16_t>(data.az * 1000.f);
  gx = static_cast<int16_t>(data.gx * 1000.f);
  gy = static_cast<int16_t>(data.gy * 1000.f);
  gz = static_cast<int16_t>(data.gz * 1000.f);
}
