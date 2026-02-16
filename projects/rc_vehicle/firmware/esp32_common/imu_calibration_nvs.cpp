#include "imu_calibration_nvs.hpp"

#include <cmath>
#include <cstring>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char* TAG = "imu_nvs";

static constexpr const char* kNvsNamespace = "imu_calib";
static constexpr const char* kNvsKey = "data";

/**
 * Формат blob в NVS (28 байт):
 *   [0]       uint8_t  version   (текущая = 1)
 *   [1]       uint8_t  flags     (bit0: gyro valid, bit1: accel valid)
 *   [2..3]    reserved (0)
 *   [4..15]   float[3] gyro_bias  (gx, gy, gz)
 *   [16..27]  float[3] accel_bias (ax, ay, az)
 */
static constexpr uint8_t kBlobVersion = 1;
static constexpr size_t kBlobSize = 28;

struct __attribute__((packed)) CalibBlob {
  uint8_t version;
  uint8_t flags;
  uint8_t reserved[2];
  float gyro_bias[3];
  float accel_bias[3];
};

static_assert(sizeof(CalibBlob) == kBlobSize, "CalibBlob size mismatch");

esp_err_t imu_nvs::Save(const ImuCalibData& data) {
  CalibBlob blob{};
  blob.version = kBlobVersion;
  blob.flags = data.valid ? 0x01 : 0x00;
  // Если accel bias ненулевой — пометить full calibration
  if (data.accel_bias[0] != 0.f || data.accel_bias[1] != 0.f ||
      data.accel_bias[2] != 0.f) {
    blob.flags |= 0x02;
  }
  std::memcpy(blob.gyro_bias, data.gyro_bias, sizeof(blob.gyro_bias));
  std::memcpy(blob.accel_bias, data.accel_bias, sizeof(blob.accel_bias));

  nvs_handle_t h;
  esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &h);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
    return err;
  }

  err = nvs_set_blob(h, kNvsKey, &blob, kBlobSize);
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);

  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Saved calib: gyro=[%.3f, %.3f, %.3f] accel=[%.4f, %.4f, %.4f]",
             data.gyro_bias[0], data.gyro_bias[1], data.gyro_bias[2],
             data.accel_bias[0], data.accel_bias[1], data.accel_bias[2]);
  } else {
    ESP_LOGE(TAG, "nvs_set_blob/commit failed: %s", esp_err_to_name(err));
  }
  return err;
}

esp_err_t imu_nvs::Load(ImuCalibData& data) {
  nvs_handle_t h;
  esp_err_t err = nvs_open(kNvsNamespace, NVS_READONLY, &h);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "No calibration data in NVS (namespace not found)");
    return ESP_ERR_NOT_FOUND;
  }

  CalibBlob blob{};
  size_t len = kBlobSize;
  err = nvs_get_blob(h, kNvsKey, &blob, &len);
  nvs_close(h);

  if (err != ESP_OK || len != kBlobSize) {
    ESP_LOGW(TAG, "NVS read failed or size mismatch (len=%u, expected=%u)",
             (unsigned)len, (unsigned)kBlobSize);
    return ESP_ERR_NOT_FOUND;
  }

  if (blob.version != kBlobVersion) {
    ESP_LOGW(TAG, "NVS blob version mismatch (%u != %u) — discarding",
             blob.version, kBlobVersion);
    return ESP_ERR_NOT_FOUND;
  }

  // Валидация: NaN / Inf / слишком большие значения
  for (int i = 0; i < 3; ++i) {
    if (!std::isfinite(blob.gyro_bias[i]) ||
        std::fabs(blob.gyro_bias[i]) > ImuCalibration::kMaxGyroBias) {
      ESP_LOGW(TAG, "Invalid gyro_bias[%d]=%.3f — discarding", i,
               blob.gyro_bias[i]);
      return ESP_ERR_INVALID_STATE;
    }
    if (!std::isfinite(blob.accel_bias[i]) ||
        std::fabs(blob.accel_bias[i]) > ImuCalibration::kMaxAccelBias) {
      ESP_LOGW(TAG, "Invalid accel_bias[%d]=%.4f — discarding", i,
               blob.accel_bias[i]);
      return ESP_ERR_INVALID_STATE;
    }
  }

  std::memcpy(data.gyro_bias, blob.gyro_bias, sizeof(data.gyro_bias));
  std::memcpy(data.accel_bias, blob.accel_bias, sizeof(data.accel_bias));
  data.valid = (blob.flags & 0x01) != 0;

  ESP_LOGI(TAG, "Loaded calib: gyro=[%.3f, %.3f, %.3f] accel=[%.4f, %.4f, %.4f] valid=%d",
           data.gyro_bias[0], data.gyro_bias[1], data.gyro_bias[2],
           data.accel_bias[0], data.accel_bias[1], data.accel_bias[2],
           data.valid);
  return ESP_OK;
}

esp_err_t imu_nvs::Erase() {
  nvs_handle_t h;
  esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &h);
  if (err != ESP_OK) return err;

  err = nvs_erase_key(h, kNvsKey);
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);

  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Calibration data erased from NVS");
  }
  return err;
}
