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
static constexpr uint8_t kCurrentVersion = 3;

/**
 * Формат blob v1 (legacy): flags, version, reserved, offset[3].
 */
struct __attribute__((packed)) MagCalibBlobV1 {
  uint8_t flags;
  uint8_t version;
  uint8_t reserved[2];
  float offset[3];
};

/**
 * Формат blob v2: flags, version, reserved, offset[3], normal[3], basis1[3],
 * basis2[3].
 */
struct __attribute__((packed)) MagCalibBlobV2 {
  uint8_t flags;        ///< bit0: valid
  uint8_t version;      ///< = 2
  uint8_t reserved[2];  ///< выравнивание
  float offset[3];      ///< hard iron offset [мГс]
  float normal[3];      ///< нормаль к плоскости вращения (единичный вектор)
  float basis1[3];      ///< ортобазис 1 в горизонтальной плоскости
  float basis2[3];      ///< ортобазис 2 в горизонтальной плоскости
};

/** Формат blob v3: v2 + эталонная RMS-величина поля после hard-iron. */
struct __attribute__((packed)) MagCalibBlobV3 {
  uint8_t flags;
  uint8_t version;
  uint8_t reserved[2];
  float offset[3];
  float normal[3];
  float basis1[3];
  float basis2[3];
  float field_strength_mgauss;
};

static constexpr size_t kBlobV1Size = sizeof(MagCalibBlobV1);
static constexpr size_t kBlobV2Size = sizeof(MagCalibBlobV2);
static constexpr size_t kBlobV3Size = sizeof(MagCalibBlobV3);

/** Максимально допустимое значение offset [мГс]. Поле Земли < 7000 мГс. */
static constexpr float kMaxOffsetMGauss = 20000.f;

esp_err_t mag_nvs::Save(const MagCalibData& data) {
  MagCalibBlobV3 blob{};
  blob.flags = data.valid ? 0x01 : 0x00;
  blob.version = kCurrentVersion;
  std::memcpy(blob.offset, data.offset, sizeof(blob.offset));
  std::memcpy(blob.normal, data.normal, sizeof(blob.normal));
  std::memcpy(blob.basis1, data.basis1, sizeof(blob.basis1));
  std::memcpy(blob.basis2, data.basis2, sizeof(blob.basis2));
  blob.field_strength_mgauss = data.field_strength_mgauss;

  nvs_handle_t h;
  esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &h);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
    return err;
  }

  err = nvs_set_blob(h, kNvsKey, &blob, kBlobV3Size);
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);

  if (err == ESP_OK) {
    ESP_LOGI(TAG,
             "Saved mag calib v3: offset=[%.1f, %.1f, %.1f] normal=[%.3f, "
             "%.3f, %.3f] field=%.1f",
             data.offset[0], data.offset[1], data.offset[2], data.normal[0],
             data.normal[1], data.normal[2], data.field_strength_mgauss);
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

  // Читаем в буфер максимального известного формата; начало v3 совместимо с
  // v2, а v1 разбирается отдельным packed-представлением.
  MagCalibBlobV3 blob{};
  size_t len = kBlobV3Size;
  err = nvs_get_blob(h, kNvsKey, &blob, &len);
  nvs_close(h);

  if (err != ESP_OK) {
    ESP_LOGW(TAG, "NVS read failed: %s", esp_err_to_name(err));
    return ESP_ERR_NOT_FOUND;
  }

  // Определяем версию
  if (len == kBlobV1Size && blob.version == 1) {
    // ── Legacy v1: offset only, default normal/basis ──────────────────
    ESP_LOGI(TAG, "Loading legacy v1 mag calib, upgrading to v2 defaults");
    auto* v1 = reinterpret_cast<MagCalibBlobV1*>(&blob);

    for (int i = 0; i < 3; ++i) {
      if (!std::isfinite(v1->offset[i]) ||
          std::fabs(v1->offset[i]) > kMaxOffsetMGauss) {
        ESP_LOGW(TAG, "Invalid v1 offset[%d]=%.2f — discarding", i,
                 v1->offset[i]);
        return ESP_ERR_INVALID_STATE;
      }
    }

    std::memcpy(data.offset, v1->offset, sizeof(data.offset));
    // Default: горизонтальный монтаж, normal=Z, basis1=X, basis2=Y
    data.normal[0] = 0.f;
    data.normal[1] = 0.f;
    data.normal[2] = 1.f;
    data.basis1[0] = 1.f;
    data.basis1[1] = 0.f;
    data.basis1[2] = 0.f;
    data.basis2[0] = 0.f;
    data.basis2[1] = 1.f;
    data.basis2[2] = 0.f;
    data.field_strength_mgauss = 0.f;
    data.valid = (v1->flags & 0x01) != 0;

    ESP_LOGI(TAG, "Loaded v1 mag calib: offset=[%.1f, %.1f, %.1f]",
             data.offset[0], data.offset[1], data.offset[2]);
    return ESP_OK;
  }

  const bool is_v2 = len == kBlobV2Size && blob.version == 2;
  const bool is_v3 = len == kBlobV3Size && blob.version == kCurrentVersion;
  if (!is_v2 && !is_v3) {
    ESP_LOGW(TAG,
             "Mag calib version/size mismatch (ver=%u, len=%u) — discarding",
             blob.version, (unsigned)len);
    return ESP_ERR_NOT_FOUND;
  }

  // ── v2/v3: полная валидация ─────────────────────────────────────────────
  for (int i = 0; i < 3; ++i) {
    if (!std::isfinite(blob.offset[i]) ||
        std::fabs(blob.offset[i]) > kMaxOffsetMGauss) {
      ESP_LOGW(TAG, "Invalid mag offset[%d]=%.2f — discarding", i,
               blob.offset[i]);
      return ESP_ERR_INVALID_STATE;
    }
    if (!std::isfinite(blob.normal[i]) || !std::isfinite(blob.basis1[i]) ||
        !std::isfinite(blob.basis2[i])) {
      ESP_LOGW(TAG, "Invalid mag normal/basis[%d] — discarding", i);
      return ESP_ERR_INVALID_STATE;
    }
  }

  // Проверить, что normal — единичный вектор (с допуском)
  const float nlen2 = blob.normal[0] * blob.normal[0] +
                      blob.normal[1] * blob.normal[1] +
                      blob.normal[2] * blob.normal[2];
  if (nlen2 < 0.8f || nlen2 > 1.2f) {
    ESP_LOGW(TAG, "Invalid normal length² = %.3f — discarding", nlen2);
    return ESP_ERR_INVALID_STATE;
  }
  if (is_v3 && (!std::isfinite(blob.field_strength_mgauss) ||
                (blob.field_strength_mgauss != 0.f &&
                 blob.field_strength_mgauss < MagCalibration::kMinRadius) ||
                blob.field_strength_mgauss > 7000.f)) {
    ESP_LOGW(TAG, "Invalid field strength %.2f — discarding",
             blob.field_strength_mgauss);
    return ESP_ERR_INVALID_STATE;
  }

  std::memcpy(data.offset, blob.offset, sizeof(data.offset));
  std::memcpy(data.normal, blob.normal, sizeof(data.normal));
  std::memcpy(data.basis1, blob.basis1, sizeof(data.basis1));
  std::memcpy(data.basis2, blob.basis2, sizeof(data.basis2));
  data.field_strength_mgauss = is_v3 ? blob.field_strength_mgauss : 0.f;
  data.valid = (blob.flags & 0x01) != 0;

  ESP_LOGI(TAG,
           "Loaded v%u mag calib: offset=[%.1f, %.1f, %.1f] normal=[%.3f, "
           "%.3f, %.3f] field=%.1f",
           blob.version, data.offset[0], data.offset[1], data.offset[2],
           data.normal[0], data.normal[1], data.normal[2],
           data.field_strength_mgauss);
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
