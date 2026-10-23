#include "motor_config_nvs.hpp"

#include <cstring>

#include "esp_log.h"
#include "nvs.h"

static const char* TAG = "motor_cfg_nvs";

static constexpr const char* kNvsNamespace = "motor_cfg";
static constexpr const char* kNvsKey = "cfg";

/** 'MOT1' — защита от чужого blob под тем же ключом. */
static constexpr uint32_t kMagic = 0x4D4F5431;

/** Текущая версия формата blob. Увеличивать при изменении структуры. */
static constexpr uint8_t kCurrentVersion = 1;

struct __attribute__((packed)) MotorConfigBlobV1 {
  uint32_t magic;
  uint8_t version;
  uint8_t profile;
  uint8_t reversed;
  uint8_t reserved;
  uint16_t min_us;
  uint16_t neutral_us;
  uint16_t max_us;
  uint16_t reserved2;
  float deadband;
};

static constexpr size_t kBlobV1Size = sizeof(MotorConfigBlobV1);

esp_err_t motor_config_nvs::Save(const rc_vehicle::MotorConfig& config) {
  if (!config.IsValid()) {
    ESP_LOGE(TAG, "Refusing to save invalid motor config");
    return ESP_ERR_INVALID_ARG;
  }

  MotorConfigBlobV1 blob{};
  blob.magic = kMagic;
  blob.version = kCurrentVersion;
  blob.profile = static_cast<uint8_t>(config.profile);
  blob.reversed = config.reversed ? 1 : 0;
  blob.min_us = config.min_us;
  blob.neutral_us = config.neutral_us;
  blob.max_us = config.max_us;
  blob.deadband = config.deadband;

  nvs_handle_t h;
  esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &h);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
    return err;
  }

  err = nvs_set_blob(h, kNvsKey, &blob, kBlobV1Size);
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);

  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Saved motor config: %s %u/%u/%u us deadband=%.3f rev=%d",
             rc_vehicle::MotorProfileToString(config.profile),
             (unsigned)config.min_us, (unsigned)config.neutral_us,
             (unsigned)config.max_us, config.deadband, (int)config.reversed);
  } else {
    ESP_LOGE(TAG, "nvs_set_blob/commit failed: %s", esp_err_to_name(err));
  }
  return err;
}

esp_err_t motor_config_nvs::Load(rc_vehicle::MotorConfig& config) {
  nvs_handle_t h;
  esp_err_t err = nvs_open(kNvsNamespace, NVS_READONLY, &h);
  if (err != ESP_OK) {
    ESP_LOGI(TAG, "No motor config in NVS, using defaults");
    return ESP_ERR_NOT_FOUND;
  }

  MotorConfigBlobV1 blob{};
  size_t len = kBlobV1Size;
  err = nvs_get_blob(h, kNvsKey, &blob, &len);
  nvs_close(h);

  if (err != ESP_OK) {
    ESP_LOGI(TAG, "No motor config in NVS (%s), using defaults",
             esp_err_to_name(err));
    return ESP_ERR_NOT_FOUND;
  }
  if (len != kBlobV1Size || blob.magic != kMagic ||
      blob.version != kCurrentVersion) {
    ESP_LOGW(TAG, "Unknown motor config format (len=%u ver=%u), ignoring",
             (unsigned)len, (unsigned)blob.version);
    return ESP_ERR_NOT_FOUND;
  }

  rc_vehicle::MotorConfig loaded{};
  loaded.profile = static_cast<rc_vehicle::MotorProfile>(blob.profile);
  loaded.reversed = blob.reversed != 0;
  loaded.min_us = blob.min_us;
  loaded.neutral_us = blob.neutral_us;
  loaded.max_us = blob.max_us;
  loaded.deadband = blob.deadband;

  // Профиль из NVS ограничен проверкой IsValid: неизвестное значение enum
  // (например, после отката прошивки) не дойдёт до выхода газа.
  if (!loaded.IsValid()) {
    ESP_LOGW(TAG, "Invalid motor config in NVS, using defaults");
    return ESP_ERR_NOT_FOUND;
  }

  config = loaded;
  return ESP_OK;
}

esp_err_t motor_config_nvs::Erase() {
  nvs_handle_t h;
  esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &h);
  if (err != ESP_OK) return err;
  err = nvs_erase_key(h, kNvsKey);
  if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  return err;
}
