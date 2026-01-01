#include "imu.hpp"

#include <math.h>
#include <string.h>

#include "config.hpp"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

// Регистры MPU-6050
#define MPU6050_REG_PWR_MGMT_1 0x6B
#define MPU6050_REG_ACCEL_XOUT_H 0x3B
#define MPU6050_REG_GYRO_XOUT_H 0x43
#define MPU6050_REG_WHO_AM_I 0x75

#define MPU6050_WHO_AM_I_VALUE 0x68

// Масштабы (зависит от настроек чувствительности)
#define MPU6050_ACCEL_SCALE (16384.0f) // ±2g: 16384 LSB/g
#define MPU6050_GYRO_SCALE (131.0f)    // ±250dps: 131 LSB/dps

static bool imu_initialized = false;

// Чтение регистра
static int mpu6050_read_reg(uint8_t reg, uint8_t *value) {
  uint8_t data[1];
  int ret = i2c_write_blocking(I2C_ID, IMU_I2C_ADDRESS, &reg, 1, true);
  if (ret < 0)
    return -1;
  ret = i2c_read_blocking(I2C_ID, IMU_I2C_ADDRESS, data, 1, false);
  if (ret < 0)
    return -1;
  *value = data[0];
  return 0;
}

// Запись регистра
static int mpu6050_write_reg(uint8_t reg, uint8_t value) {
  uint8_t data[2] = {reg, value};
  int ret = i2c_write_blocking(I2C_ID, IMU_I2C_ADDRESS, data, 2, false);
  return (ret < 0) ? -1 : 0;
}

// Чтение 16-битного значения (big-endian)
static int mpu6050_read_reg16(uint8_t reg, int16_t *value) {
  uint8_t data[2];
  int ret = i2c_write_blocking(I2C_ID, IMU_I2C_ADDRESS, &reg, 1, true);
  if (ret < 0)
    return -1;
  ret = i2c_read_blocking(I2C_ID, IMU_I2C_ADDRESS, data, 2, false);
  if (ret < 0)
    return -1;
  *value = (int16_t)((data[0] << 8) | data[1]);
  return 0;
}

int ImuInit(void) {
  if (imu_initialized) {
    return 0;
  }

  // Инициализация I2C
  i2c_init(I2C_ID, I2C_FREQUENCY_HZ);
  gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
  gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
  gpio_pull_up(I2C_SDA_PIN);
  gpio_pull_up(I2C_SCL_PIN);

  // Проверка WHO_AM_I
  uint8_t who_am_i = 0;
  if (mpu6050_read_reg(MPU6050_REG_WHO_AM_I, &who_am_i) < 0) {
    return -1;
  }
  if (who_am_i != MPU6050_WHO_AM_I_VALUE) {
    return -1;
  }

  // Пробуждение MPU-6050 (сброс бита SLEEP)
  if (mpu6050_write_reg(MPU6050_REG_PWR_MGMT_1, 0x00) < 0) {
    return -1;
  }

  // Небольшая задержка для стабилизации
  sleep_ms(10);

  imu_initialized = true;
  return 0;
}

int ImuRead(ImuData *data) {
  if (data == NULL || !imu_initialized) {
    return -1;
  }

  int16_t raw_ax, raw_ay, raw_az;
  int16_t raw_gx, raw_gy, raw_gz;

  // Чтение акселерометра
  if (mpu6050_read_reg16(MPU6050_REG_ACCEL_XOUT_H, &raw_ax) < 0)
    return -1;
  if (mpu6050_read_reg16(MPU6050_REG_ACCEL_XOUT_H + 2, &raw_ay) < 0)
    return -1;
  if (mpu6050_read_reg16(MPU6050_REG_ACCEL_XOUT_H + 4, &raw_az) < 0)
    return -1;

  // Чтение гироскопа
  if (mpu6050_read_reg16(MPU6050_REG_GYRO_XOUT_H, &raw_gx) < 0)
    return -1;
  if (mpu6050_read_reg16(MPU6050_REG_GYRO_XOUT_H + 2, &raw_gy) < 0)
    return -1;
  if (mpu6050_read_reg16(MPU6050_REG_GYRO_XOUT_H + 4, &raw_gz) < 0)
    return -1;

  // Конвертация в физические единицы
  data->ax = (float)raw_ax / MPU6050_ACCEL_SCALE;
  data->ay = (float)raw_ay / MPU6050_ACCEL_SCALE;
  data->az = (float)raw_az / MPU6050_ACCEL_SCALE;

  data->gx = (float)raw_gx / MPU6050_GYRO_SCALE;
  data->gy = (float)raw_gy / MPU6050_GYRO_SCALE;
  data->gz = (float)raw_gz / MPU6050_GYRO_SCALE;

  return 0;
}

void ImuConvertToTelem(const ImuData *data, int16_t *ax, int16_t *ay,
                       int16_t *az, int16_t *gx, int16_t *gy, int16_t *gz) {
  if (data == NULL || ax == NULL || ay == NULL || az == NULL || gx == NULL ||
      gy == NULL || gz == NULL) {
    return;
  }

  // Конвертация в миллиграммы (mg) и миллиградусы в секунду (mdps)
  *ax = (int16_t)(data->ax * 1000.0f);
  *ay = (int16_t)(data->ay * 1000.0f);
  *az = (int16_t)(data->az * 1000.0f);

  *gx = (int16_t)(data->gx * 1000.0f);
  *gy = (int16_t)(data->gy * 1000.0f);
  *gz = (int16_t)(data->gz * 1000.0f);
}
