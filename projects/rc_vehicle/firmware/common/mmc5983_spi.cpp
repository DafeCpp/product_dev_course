#include "mmc5983_spi.hpp"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static const char* MMC_TAG = "mmc5983_spi";
#endif

// Регистры вывода (18-bit: 16 старших + 2 младших в OL-регистре)
#define MMC5983_REG_XOUT_H   0x00  // X[17:10]
#define MMC5983_REG_XOUT_L   0x01  // X[9:2]
#define MMC5983_REG_YOUT_H   0x02  // Y[17:10]
#define MMC5983_REG_YOUT_L   0x03  // Y[9:2]
#define MMC5983_REG_ZOUT_H   0x04  // Z[17:10]
#define MMC5983_REG_ZOUT_L   0x05  // Z[9:2]
#define MMC5983_REG_XYZ_OL   0x06  // bits[7:6]=X[1:0], [5:4]=Y[1:0], [3:2]=Z[1:0]
#define MMC5983_REG_STATUS   0x08  // Meas_M_Done (бит 0), Meas_T_Done (бит 1)
#define MMC5983_REG_CTRL0    0x09  // TM_M (бит 0), SET (бит 3), RESET (бит 4)
#define MMC5983_REG_CTRL1    0x0A  // SW_RST (бит 7), BW[1:0] (биты 1:0)
#define MMC5983_REG_CTRL2    0x0B  // CMM_en (бит 4), CMM_freq[2:0] (биты 2:0)
#define MMC5983_REG_PRODUCT  0x2F  // Product ID

// Product ID
#define MMC5983_PRODUCT_ID   0x30

// Бит чтения (бит 7 адреса = 1 для чтения)
#define MMC5983_SPI_READ_BIT 0x80

// CTRL1: BW = 00 (100 Гц пропускная полоса)
#define MMC5983_CTRL1_BW_100HZ 0x00

// CTRL2: CMM_en=1, CMM_freq=100Hz (000 → 1 Гц, 001 → 10 Гц, 010 → 20 Гц,
//         011 → 50 Гц, 100 → 100 Гц, 101 → 200 Гц, 110 → 1000 Гц)
#define MMC5983_CTRL2_CMM_100HZ 0x14  // CMM_en (бит 4) | freq=100Hz (0b100)

// STATUS: Meas_M_Done
#define MMC5983_STATUS_MEAS_M_DONE 0x01

// Масштаб: 18-битный диапазон 0..262143, ноль поля = 131072 (2^17).
// Полная шкала ±8 Гс = ±800 мГс. 1 LSB = 800/131072 мГс ≈ 0.006104 мГс.
#define MMC5983_HALF_RANGE    131072.0f
#define MMC5983_SCALE_MGAUSS  (800.0f / 131072.0f)

int Mmc5983Spi::ReadReg(uint8_t reg, uint8_t& value) {
  uint8_t tx[2] = {static_cast<uint8_t>(reg | MMC5983_SPI_READ_BIT), 0};
  uint8_t rx[2] = {0, 0};
  if (spi_->Transfer(std::span<const uint8_t>(tx), std::span<uint8_t>(rx)) != 0)
    return -1;
  value = rx[1];
  return 0;
}

int Mmc5983Spi::WriteReg(uint8_t reg, uint8_t value) {
  uint8_t tx[2] = {reg, value};
  uint8_t rx[2] = {0, 0};
  return spi_->Transfer(std::span<const uint8_t>(tx), std::span<uint8_t>(rx)) == 0
             ? 0
             : -1;
}

int Mmc5983Spi::DoSet() {
  // SET: записать бит 3 в CTRL0, после чего датчик автоматически сбрасывает бит
  return WriteReg(MMC5983_REG_CTRL0, 0x08);
}

int Mmc5983Spi::DoReset() {
  // RESET: записать бит 4 в CTRL0
  return WriteReg(MMC5983_REG_CTRL0, 0x10);
}

int Mmc5983Spi::Init() {
  if (initialized_)
    return 0;
  if (spi_->Init() != 0)
    return -1;

  // Программный сброс
  (void)WriteReg(MMC5983_REG_CTRL1, 0x80);

#ifdef ESP_PLATFORM
  vTaskDelay(pdMS_TO_TICKS(20));
#endif

  // Проверка Product ID с повторными попытками
  uint8_t product_id = 0;
  constexpr int kMaxRetries = 5;
  for (int attempt = 0; attempt < kMaxRetries; ++attempt) {
    int rc = ReadReg(MMC5983_REG_PRODUCT, product_id);
#ifdef ESP_PLATFORM
    ESP_LOGI(MMC_TAG, "Product ID attempt %d: rc=%d, value=0x%02X", attempt, rc,
             product_id);
#endif
    if (rc == 0 && product_id == MMC5983_PRODUCT_ID)
      break;
#ifdef ESP_PLATFORM
    vTaskDelay(pdMS_TO_TICKS(20));
#endif
  }

  last_product_id_ = static_cast<int>(product_id);
  if (product_id != MMC5983_PRODUCT_ID)
    return -1;

  // SET-импульс: намагничивание в прямом направлении для удаления остаточной намагниченности
  if (DoSet() != 0)
    return -1;

#ifdef ESP_PLATFORM
  vTaskDelay(pdMS_TO_TICKS(1));
#endif

  // BW = 100 Гц (минимальный шум при достаточной скорости)
  if (WriteReg(MMC5983_REG_CTRL1, MMC5983_CTRL1_BW_100HZ) != 0)
    return -1;

  // Запуск Continuous Measurement Mode на 100 Гц
  if (WriteReg(MMC5983_REG_CTRL2, MMC5983_CTRL2_CMM_100HZ) != 0)
    return -1;

  initialized_ = true;
  read_count_ = 0;
  return 0;
}

int Mmc5983Spi::Read(MagData& data) {
  if (!initialized_)
    return -1;

  // Периодическое SET/RESET для компенсации температурного дрейфа.
  // Чередуем: на чётных периодах — SET, на нечётных — RESET.
  if (read_count_ > 0 && (read_count_ % kSetResetPeriod) == 0) {
    if ((read_count_ / kSetResetPeriod) % 2 == 0) {
      (void)DoSet();
    } else {
      (void)DoReset();
    }
  }
  ++read_count_;

  // Бёрст-чтение 7 байт (0x00-0x06): X_H, X_L, Y_H, Y_L, Z_H, Z_L, XYZ_OL
  uint8_t tx[8] = {static_cast<uint8_t>(MMC5983_REG_XOUT_H | MMC5983_SPI_READ_BIT)};
  uint8_t rx[8] = {};
  if (spi_->Transfer(std::span<const uint8_t>(tx, 8), std::span<uint8_t>(rx, 8)) != 0)
    return -1;

  // Сборка 18-битных значений: старшие 16 бит из H/L-регистров, младшие 2 бита из OL.
  const uint8_t ol = rx[7];
  const uint32_t raw_x = (static_cast<uint32_t>(rx[1]) << 10) |
                          (static_cast<uint32_t>(rx[2]) << 2) |
                          ((ol >> 6) & 0x03);
  const uint32_t raw_y = (static_cast<uint32_t>(rx[3]) << 10) |
                          (static_cast<uint32_t>(rx[4]) << 2) |
                          ((ol >> 4) & 0x03);
  const uint32_t raw_z = (static_cast<uint32_t>(rx[5]) << 10) |
                          (static_cast<uint32_t>(rx[6]) << 2) |
                          ((ol >> 2) & 0x03);

  // Вычитаем ноль поля (131072) и переводим в мГс
  data.mx = (static_cast<float>(raw_x) - MMC5983_HALF_RANGE) * MMC5983_SCALE_MGAUSS;
  data.my = (static_cast<float>(raw_y) - MMC5983_HALF_RANGE) * MMC5983_SCALE_MGAUSS;
  data.mz = (static_cast<float>(raw_z) - MMC5983_HALF_RANGE) * MMC5983_SCALE_MGAUSS;

  return 0;
}
