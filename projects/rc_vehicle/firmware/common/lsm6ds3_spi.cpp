#include "lsm6ds3_spi.hpp"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static const char *LSM_TAG = "lsm6ds3_spi";
#endif

// Регистры LSM6DS3/LSM6DSL
#define LSM6DS3_REG_WHO_AM_I 0x0F
#define LSM6DS3_REG_CTRL1_XL 0x10  // Акселерометр: ODR + FS
#define LSM6DS3_REG_CTRL2_G  0x11  // Гироскоп: ODR + FS
#define LSM6DS3_REG_CTRL3_C  0x12  // BDU, IF_INC
#define LSM6DS3_REG_OUTX_L_G 0x22  // Начало блока выходных данных (gyro + accel)

#define LSM6DS3_WHO_AM_I_VALUE  0x6A  // LSM6DS3
#define LSM6DSL_WHO_AM_I_VALUE  0x6C  // LSM6DSL (совместим)
#define LSM6DS3_SPI_READ_BIT    0x80

// Конфигурация:
// CTRL1_XL = 0x60: ODR_XL=416Hz (0110), FS_XL=±2g (00)
#define LSM6DS3_CTRL1_XL_VAL 0x60
// CTRL2_G  = 0x60: ODR_G=416Hz (0110), FS_G=±250dps (00)
#define LSM6DS3_CTRL2_G_VAL  0x60
// CTRL3_C  = 0x44: BDU=1 (бит 6), IF_INC=1 (бит 2)
#define LSM6DS3_CTRL3_C_VAL  0x44

// Масштабирование
#define LSM6DS3_ACCEL_SCALE 16384.0f   // LSB/g при ±2g
#define LSM6DS3_GYRO_SCALE  114.286f   // LSB/dps при ±250dps (8.75 mdps/LSB)

int Lsm6ds3Spi::ReadReg(uint8_t reg, uint8_t &value) {
  uint8_t tx[2] = {static_cast<uint8_t>(reg | LSM6DS3_SPI_READ_BIT), 0};
  uint8_t rx[2] = {0, 0};
  if (spi_->Transfer(std::span<const uint8_t>(tx), std::span<uint8_t>(rx)) != 0)
    return -1;
  value = rx[1];
  return 0;
}

int Lsm6ds3Spi::WriteReg(uint8_t reg, uint8_t value) {
  uint8_t tx[2] = {reg, value};
  uint8_t rx[2] = {0, 0};
  return spi_->Transfer(std::span<const uint8_t>(tx), std::span<uint8_t>(rx)) == 0
             ? 0
             : -1;
}

int Lsm6ds3Spi::Init() {
  if (initialized_)
    return 0;
  if (spi_->Init() != 0)
    return -1;

  // Программный сброс через CTRL3_C.SW_RESET (бит 0)
  (void)WriteReg(LSM6DS3_REG_CTRL3_C, 0x01);

#ifdef ESP_PLATFORM
  vTaskDelay(pdMS_TO_TICKS(50));
#endif

  // Проверка WHO_AM_I с повторными попытками
  uint8_t who_am_i = 0;
  constexpr int kMaxRetries = 5;
  for (int attempt = 0; attempt < kMaxRetries; ++attempt) {
    int rc = ReadReg(LSM6DS3_REG_WHO_AM_I, who_am_i);
#ifdef ESP_PLATFORM
    ESP_LOGI(LSM_TAG, "WHO_AM_I attempt %d: rc=%d, value=0x%02X", attempt, rc,
             who_am_i);
#endif
    if (rc == 0 && (who_am_i == LSM6DS3_WHO_AM_I_VALUE ||
                    who_am_i == LSM6DSL_WHO_AM_I_VALUE)) {
      break;
    }
#ifdef ESP_PLATFORM
    vTaskDelay(pdMS_TO_TICKS(50));
#endif
  }

  last_who_am_i_ = static_cast<int>(who_am_i);
  if (who_am_i != LSM6DS3_WHO_AM_I_VALUE && who_am_i != LSM6DSL_WHO_AM_I_VALUE)
    return -1;

  // Настройка акселерометра: ODR=416Hz, FS=±2g
  if (WriteReg(LSM6DS3_REG_CTRL1_XL, LSM6DS3_CTRL1_XL_VAL) != 0)
    return -1;
  // Настройка гироскопа: ODR=416Hz, FS=±250dps
  if (WriteReg(LSM6DS3_REG_CTRL2_G, LSM6DS3_CTRL2_G_VAL) != 0)
    return -1;
  // Общие настройки: BDU=1, IF_INC=1
  if (WriteReg(LSM6DS3_REG_CTRL3_C, LSM6DS3_CTRL3_C_VAL) != 0)
    return -1;

  initialized_ = true;
  return 0;
}

int Lsm6ds3Spi::Read(ImuData &data) {
  if (!initialized_)
    return -1;

  // Бёрст-чтение 12 байт: 6 gyro + 6 accel (с 0x22, порядок little-endian)
  uint8_t tx[13] = {static_cast<uint8_t>(LSM6DS3_REG_OUTX_L_G | LSM6DS3_SPI_READ_BIT)};
  uint8_t rx[13] = {};
  if (spi_->Transfer(std::span<const uint8_t>(tx, 13), std::span<uint8_t>(rx, 13)) != 0)
    return -1;

  // LSM6DS3: little-endian (LSB first)
  auto to16 = [&](int i) -> int16_t {
    return static_cast<int16_t>(static_cast<uint16_t>(rx[i]) |
                                 (static_cast<uint16_t>(rx[i + 1]) << 8));
  };

  const int16_t raw_gx = to16(1);
  const int16_t raw_gy = to16(3);
  const int16_t raw_gz = to16(5);
  const int16_t raw_ax = to16(7);
  const int16_t raw_ay = to16(9);
  const int16_t raw_az = to16(11);

  data.gx = static_cast<float>(raw_gx) / LSM6DS3_GYRO_SCALE;
  data.gy = static_cast<float>(raw_gy) / LSM6DS3_GYRO_SCALE;
  data.gz = static_cast<float>(raw_gz) / LSM6DS3_GYRO_SCALE;
  data.ax = static_cast<float>(raw_ax) / LSM6DS3_ACCEL_SCALE;
  data.ay = static_cast<float>(raw_ay) / LSM6DS3_ACCEL_SCALE;
  data.az = static_cast<float>(raw_az) / LSM6DS3_ACCEL_SCALE;

  return 0;
}
