#include "mmc5983_i2c.hpp"

#ifdef ESP_PLATFORM

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* MMC_TAG = "mmc5983_i2c";

// Регистры (идентичны SPI-версии)
#define MMC5983_REG_XOUT_H   0x00
#define MMC5983_REG_XOUT_L   0x01
#define MMC5983_REG_YOUT_H   0x02
#define MMC5983_REG_YOUT_L   0x03
#define MMC5983_REG_ZOUT_H   0x04
#define MMC5983_REG_ZOUT_L   0x05
#define MMC5983_REG_XYZ_OL   0x06
#define MMC5983_REG_STATUS   0x08
#define MMC5983_REG_CTRL0    0x09
#define MMC5983_REG_CTRL1    0x0A
#define MMC5983_REG_CTRL2    0x0B
#define MMC5983_REG_PRODUCT  0x2F

#define MMC5983_PRODUCT_ID       0x30
#define MMC5983_CTRL1_BW_100HZ   0x00
#define MMC5983_CTRL2_CMM_100HZ  0x14  // CMM_en | freq=100Hz
#define MMC5983_HALF_RANGE       131072.0f
#define MMC5983_SCALE_MGAUSS     (8000.0f / 131072.0f)

static constexpr int kI2cTimeoutMs = 50;

// ─── Низкоуровневые I2C-операции ──────────────────────────────────────────

int Mmc5983I2c::WriteReg(uint8_t reg, uint8_t value) noexcept {
  uint8_t buf[2] = {reg, value};
  esp_err_t err = i2c_master_transmit(dev_handle_, buf, 2,
                                      pdMS_TO_TICKS(kI2cTimeoutMs));
  return (err == ESP_OK) ? 0 : -1;
}

int Mmc5983I2c::ReadRegs(uint8_t reg, uint8_t* buf, size_t len) noexcept {
  // I2C: отправить адрес регистра, затем прочитать данные
  esp_err_t err = i2c_master_transmit_receive(
      dev_handle_, &reg, 1, buf, len, pdMS_TO_TICKS(kI2cTimeoutMs));
  return (err == ESP_OK) ? 0 : -1;
}

int Mmc5983I2c::DoSet() noexcept {
  return WriteReg(MMC5983_REG_CTRL0, 0x08);
}

int Mmc5983I2c::DoReset() noexcept {
  return WriteReg(MMC5983_REG_CTRL0, 0x10);
}

// ─── Инициализация ────────────────────────────────────────────────────────

int Mmc5983I2c::Init(const Config& cfg) noexcept {
  if (initialized_) return 0;

  // Создать I2C master шину
  i2c_master_bus_config_t bus_cfg{};
  bus_cfg.i2c_port            = cfg.port;
  bus_cfg.sda_io_num          = cfg.sda_pin;
  bus_cfg.scl_io_num          = cfg.scl_pin;
  bus_cfg.clk_source          = I2C_CLK_SRC_DEFAULT;
  bus_cfg.glitch_ignore_cnt   = 7;
  bus_cfg.flags.enable_internal_pullup = true;

  esp_err_t err = i2c_new_master_bus(&bus_cfg, &bus_handle_);
  if (err != ESP_OK) {
    ESP_LOGE(MMC_TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
    return -1;
  }

  // Добавить устройство
  i2c_device_config_t dev_cfg{};
  dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev_cfg.device_address  = kAddr;
  dev_cfg.scl_speed_hz    = cfg.speed_hz;

  err = i2c_master_bus_add_device(bus_handle_, &dev_cfg, &dev_handle_);
  if (err != ESP_OK) {
    ESP_LOGE(MMC_TAG, "i2c_master_bus_add_device failed: %s", esp_err_to_name(err));
    i2c_del_master_bus(bus_handle_);
    bus_handle_ = nullptr;
    return -1;
  }

  // SW reset
  (void)WriteReg(MMC5983_REG_CTRL1, 0x80);
  vTaskDelay(pdMS_TO_TICKS(20));

  // Проверка Product ID с повторными попытками
  uint8_t product_id = 0;
  constexpr int kMaxRetries = 5;
  for (int attempt = 0; attempt < kMaxRetries; ++attempt) {
    int rc = ReadRegs(MMC5983_REG_PRODUCT, &product_id, 1);
    ESP_LOGI(MMC_TAG, "Product ID attempt %d: rc=%d, value=0x%02X",
             attempt, rc, product_id);
    if (rc == 0 && product_id == MMC5983_PRODUCT_ID) break;
    vTaskDelay(pdMS_TO_TICKS(20));
  }

  last_product_id_ = static_cast<int>(product_id);
  if (product_id != MMC5983_PRODUCT_ID) {
    ESP_LOGE(MMC_TAG, "Product ID mismatch: 0x%02X (expected 0x30)", product_id);
    return -1;
  }

  // SET-импульс
  if (DoSet() != 0) return -1;
  vTaskDelay(pdMS_TO_TICKS(1));

  // BW = 100 Гц
  if (WriteReg(MMC5983_REG_CTRL1, MMC5983_CTRL1_BW_100HZ) != 0) return -1;

  // CMM @ 100 Гц
  if (WriteReg(MMC5983_REG_CTRL2, MMC5983_CTRL2_CMM_100HZ) != 0) return -1;

  initialized_ = true;
  read_count_  = 0;
  ESP_LOGI(MMC_TAG, "MMC5983MA I2C инициализирован (addr=0x%02X, %lu Гц)",
           kAddr, static_cast<unsigned long>(cfg.speed_hz));
  return 0;
}

// ─── Чтение ───────────────────────────────────────────────────────────────

int Mmc5983I2c::Read(MagData& data) {
  if (!initialized_) return -1;

  // Периодическое SET/RESET
  if (read_count_ > 0 && (read_count_ % kSetResetPeriod) == 0) {
    if ((read_count_ / kSetResetPeriod) % 2 == 0)
      (void)DoSet();
    else
      (void)DoReset();
  }
  ++read_count_;

  // Burst read 7 байт: регистры 0x00-0x06
  uint8_t rx[7] = {};
  if (ReadRegs(MMC5983_REG_XOUT_H, rx, 7) != 0) return -1;

  const uint8_t ol = rx[6];
  const uint32_t raw_x = (static_cast<uint32_t>(rx[0]) << 10) |
                          (static_cast<uint32_t>(rx[1]) << 2)  |
                          ((ol >> 6) & 0x03);
  const uint32_t raw_y = (static_cast<uint32_t>(rx[2]) << 10) |
                          (static_cast<uint32_t>(rx[3]) << 2)  |
                          ((ol >> 4) & 0x03);
  const uint32_t raw_z = (static_cast<uint32_t>(rx[4]) << 10) |
                          (static_cast<uint32_t>(rx[5]) << 2)  |
                          ((ol >> 2) & 0x03);

  data.mx = (static_cast<float>(raw_x) - MMC5983_HALF_RANGE) * MMC5983_SCALE_MGAUSS;
  data.my = (static_cast<float>(raw_y) - MMC5983_HALF_RANGE) * MMC5983_SCALE_MGAUSS;
  data.mz = (static_cast<float>(raw_z) - MMC5983_HALF_RANGE) * MMC5983_SCALE_MGAUSS;
  return 0;
}

#endif  // ESP_PLATFORM
