#include "mag_calibration_nvs.hpp"

#include <cmath>
#include <cstring>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char* TAG = "mag_nvs";

static constexpr const char* kNvsNamespace = "mag_calib";
static constexpr const char* kNvsKey = "data";

/** Текущая версия формата blob. Увеличивать при изменении структуры. */
static constexpr uint8_t kCurrentVersion = 1;

/**
 * Формат blob v1: флаги, версия, зарезервировано, offset X/Y/Z.
 */
struct __attribute__((packed)) MagCalibBlob {
  uint8_t flags;        ///< bit0: valid
  uint8_t version;      ///< версия формата
  uint8_t reserved[2];  ///< выравнивание
  float offset[3];      ///< hard iron offset [мГс]
};

static constexpr size_t kBlobSize = sizeof(MagCalibBlob);

/** Максимально допустимое значение offset [мГс]. Поле Земли < 700 мГс. */
static constexpr float kMaxOffsetMGauss = 2000.f;

esp_err_t mag_nvs::Save(const MagCalibData& data) {
  MagCalibBlob blob{};
  blob.flags = data.valid ? 0x01 : 0x00;
  blob.version = kCurrentVersion;
  std::memcpy(blob.offset, data.offset, sizeof(blob.offset));

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
    ESP_LOGI(TAG, "Saved mag calib: offset=[%.2f, %.2f, %.2f] mGauss",
             data.offset[0], data.offset[1], data.offset[2]);
  } else {
    ESP_LOGE(TAG, "nvs_set_blob/commit failed: %s", esp_err_to_name(err));
  }
  return err;
}

esp_err_t mag_nvs::Load(MagCalibData& data) {
  nvs_handle_t h;
  esp_err_t err = nvs_open(kNvsNamespace, NVS_READONLY, &h);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "No mag calibration data in NVS (namespace not found)");
    return ESP_ERR_NOT_FOUND;
  }

  MagCalibBlob blob{};
  size_t len = kBlobSize;
  err = nvs_get_blob(h, kNvsKey, &blob, &len);
  nvs_close(h);

  if (err != ESP_OK || len != kBlobSize) {
    ESP_LOGW(TAG, "NVS read failed or size mismatch (len=%u, expected=%u)",
             (unsigned)len, (unsigned)kBlobSize);
    return ESP_ERR_NOT_FOUND;
  }

  if (blob.version != kCurrentVersion) {
    ESP_LOGW(TAG,
             "Mag calib version mismatch (got=%u, expected=%u) — discarding",
             blob.version, kCurrentVersion);
    return ESP_ERR_NOT_FOUND;
  }

  // Валидация значений
  for (int i = 0; i < 3; ++i) {
    if (!std::isfinite(blob.offset[i]) ||
        std::fabs(blob.offset[i]) > kMaxOffsetMGauss) {
      ESP_LOGW(TAG, "Invalid mag offset[%d]=%.2f — discarding", i,
               blob.offset[i]);
      return ESP_ERR_INVALID_STATE;
    }
  }

  std::memcpy(data.offset, blob.offset, sizeof(data.offset));
  data.valid = (blob.flags & 0x01) != 0;

  ESP_LOGI(TAG,
           "Loaded mag calib: offset=[%.2f, %.2f, %.2f] mGauss valid=%d",
           data.offset[0], data.offset[1], data.offset[2], data.valid);
  return ESP_OK;
}

esp_err_t mag_nvs::Erase() {
  nvs_handle_t h;
  esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &h);
  if (err != ESP_OK) return err;

  err = nvs_erase_key(h, kNvsKey);
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);

  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Mag calibration data erased from NVS");
  }
  return err;
}
