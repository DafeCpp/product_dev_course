#include "stabilization_config_nvs.hpp"

#include <cstddef>
#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

using rc_vehicle::DriveMode;
using rc_vehicle::StabilizationConfig;

static const char* TAG = "stab_cfg_nvs";
static const char* NVS_NAMESPACE = "stab_cfg";
// Legacy однослотовый ключ (до per-mode). Сохранён для миграции.
static const char* NVS_LEGACY_KEY = "config";
// Ключ с номером активного режима (uint8).
static const char* NVS_ACTIVE_KEY = "active";

/** Число режимов DriveMode (Normal..DirectLaw). */
static constexpr uint8_t kDriveModeCount = 5;

/** Текущая версия формата. Увеличивать при изменении StabilizationConfig. */
// v2: добавлены FilterConfig::adaptive_beta_enabled, adaptive_accel_threshold_g
// v3: добавлены slew_throttle, slew_steering в StabilizationConfig
// v4: добавлены FilterConfig::madgwick_enabled, ekf_enabled
// v5: добавлены KidsModeConfig::speed_limit_enabled, max_speed_ms,
// speed_limit_gain v6: добавлены StabilizationConfig::braking_mode,
// brake_slew_multiplier v7: добавлены FilterConfig мотор-модель/NHC
// (motor_model_enabled,
//     motor_speed_gain, motor_deadzone, speed_meas_noise, nhc_enabled,
//     nhc_noise)
// v8: добавлены FilterConfig::tilt_comp_enabled, tilt_corr_gain_hz,
//     tilt_accel_gate_band_g (LOS-240)
// v9: сброс Kids-профиля для обновлённого slew_steering (LOS-248)
// v10: миграция KidsModeConfig::speed_limit_gain на 1.5 (LOS-247)
// v11: безопасный Kids yaw-rate пресет (LOS-284)
// v12: добавлен KidsModeConfig::limiters_enabled (LOS-286)
// v13: новые yaw-коэффициенты Normal вместо нетронутых прежних (LOS-366)
// v14: то же для Sport (LOS-371)
// v15: добавлен StabilizationConfig::roll_comp перед version (LOS-386);
//      блобы прежнего размера расширяются MigratePreRollCompLayout()
// v16: новый slip-PID и slew газа Drift вместо нетронутых прежних (LOS-380)
static constexpr uint8_t kCurrentStabConfigVersion = 16;
static constexpr uint8_t kV8StabConfigVersion = 8;
static constexpr uint8_t kV9StabConfigVersion = 9;
static constexpr uint8_t kV10StabConfigVersion = 10;
static constexpr uint8_t kV11StabConfigVersion = 11;
static constexpr uint8_t kV12StabConfigVersion = 12;
static constexpr uint8_t kV13StabConfigVersion = 13;
static constexpr uint8_t kV14StabConfigVersion = 14;
static constexpr uint8_t kV15StabConfigVersion = 15;

/** Обёртка с версионным заголовком для NVS-хранения. */
struct StabConfigBlob {
  uint8_t version;
  uint8_t reserved[3];
  StabilizationConfig config;
};
static_assert(offsetof(StabConfigBlob, config) == 4,
              "StabConfigBlob header must stay 4 bytes");
/** Размер блоба до LOS-386 (без roll_comp). */
static constexpr size_t kPreRollCompBlobSize =
    offsetof(StabConfigBlob, config) +
    rc_vehicle::kStabilizationConfigPreRollCompSize;

namespace {

/** Сформировать ключ слота режима: "cfg0".."cfg4". */
void ModeKey(DriveMode mode, char (&out)[8]) {
  std::snprintf(out, sizeof(out), "cfg%u", static_cast<unsigned>(mode));
}

void ApplyKidsYawSafetyPreset(StabilizationConfig& config) {
  config.yaw_rate.pid.kp = 0.005f;
  config.yaw_rate.pid.ki = 0.0f;
  config.yaw_rate.pid.kd = 0.0f;
  config.yaw_rate.pid.max_correction = 0.15f;
  config.adaptive.scale_max = 1.0f;
}

/** Прочитать blob по ключу из открытого хэндла, провалидировать. */
esp_err_t ReadBlobKey(nvs_handle_t handle, const char* key,
                      StabilizationConfig& config) {
  StabConfigBlob blob{};
  size_t required_size = sizeof(StabConfigBlob);
  esp_err_t err = nvs_get_blob(handle, key, &blob, &required_size);
  if (err != ESP_OK) {
    return err;
  }
  if (required_size != sizeof(StabConfigBlob)) {
    ESP_LOGW(TAG, "Config size mismatch (got=%zu expected=%zu) — discarding",
             required_size, sizeof(StabConfigBlob));
    return ESP_ERR_NOT_FOUND;
  }
  if (blob.version != kCurrentStabConfigVersion) {
    ESP_LOGW(TAG, "Config version mismatch (got=%u expected=%u) — discarding",
             blob.version, kCurrentStabConfigVersion);
    return ESP_ERR_NOT_FOUND;
  }
  if (!blob.config.IsValid()) {
    ESP_LOGW(TAG, "Loaded config failed validation — discarding");
    return ESP_ERR_INVALID_STATE;
  }
  config = blob.config;
  config.Clamp();
  return ESP_OK;
}

}  // namespace

namespace stab_config_nvs {

/**
 * @brief Расширить блобы прежнего размера под roll_comp (LOS-386).
 *
 * Все остальные миграции работают только с блобами текущего размера, поэтому
 * этот шаг идёт первым: он переносит пользовательские настройки любой версии
 * старой раскладки (v8–v14) в новую, сохраняя номер версии, чтобы дальше
 * отработала обычная цепочка. roll_comp получает значения по умолчанию.
 */
esp_err_t MigratePreRollCompLayout(nvs_handle_t handle) {
  bool changed = false;
  auto widen = [&](const char* key) -> esp_err_t {
    uint8_t raw[sizeof(StabConfigBlob)] = {};
    size_t size = sizeof(raw);
    esp_err_t err = nvs_get_blob(handle, key, raw, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    if (size != kPreRollCompBlobSize) return ESP_OK;

    StabConfigBlob blob{};
    blob.version = raw[0];
    if (!rc_vehicle::StabilizationConfigFromPreRollComp(
            raw + offsetof(StabConfigBlob, config),
            size - offsetof(StabConfigBlob, config), blob.config)) {
      return ESP_OK;
    }
    err = nvs_set_blob(handle, key, &blob, sizeof(blob));
    if (err != ESP_OK) return err;
    ESP_LOGI(TAG, "Widened %s (v%u) for roll_comp", key,
             static_cast<unsigned>(blob.version));
    changed = true;
    return ESP_OK;
  };

  for (uint8_t m = 0; m < kDriveModeCount; ++m) {
    char key[8];
    ModeKey(static_cast<DriveMode>(m), key);
    const esp_err_t err = widen(key);
    if (err != ESP_OK) return err;
  }
  const esp_err_t err = widen(NVS_LEGACY_KEY);
  if (err != ESP_OK) return err;

  return changed ? nvs_commit(handle) : ESP_OK;
}

/** Мигрировать все per-mode профили v8 в v9, сбросив только Kids. */
esp_err_t MigrateV8Profiles(nvs_handle_t handle) {
  bool changed = false;
  for (uint8_t m = 0; m < kDriveModeCount; ++m) {
    char key[8];
    const DriveMode mode = static_cast<DriveMode>(m);
    ModeKey(mode, key);

    StabConfigBlob blob{};
    size_t size = sizeof(blob);
    esp_err_t err = nvs_get_blob(handle, key, &blob, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
      continue;
    }
    if (err != ESP_OK) {
      return err;
    }
    if (size != sizeof(blob) || blob.version != kV8StabConfigVersion) {
      continue;
    }

    if (mode == DriveMode::Kids) {
      blob.config.Reset();
      blob.config.kids_mode = rc_vehicle::KidsModeConfig{};
      blob.config.mode = DriveMode::Kids;
      blob.config.ApplyModeDefaults();
      ESP_LOGI(TAG, "Reset Kids config while migrating NVS v8 -> v9");
    }
    blob.version = kV9StabConfigVersion;
    err = nvs_set_blob(handle, key, &blob, sizeof(blob));
    if (err != ESP_OK) {
      return err;
    }
    changed = true;
  }

  StabConfigBlob legacy_blob{};
  size_t legacy_size = sizeof(legacy_blob);
  esp_err_t legacy_err =
      nvs_get_blob(handle, NVS_LEGACY_KEY, &legacy_blob, &legacy_size);
  if (legacy_err != ESP_ERR_NVS_NOT_FOUND && legacy_err != ESP_OK) {
    return legacy_err;
  }
  if (legacy_err == ESP_OK && legacy_size == sizeof(legacy_blob) &&
      legacy_blob.version == kV8StabConfigVersion) {
    if (legacy_blob.config.mode == DriveMode::Kids) {
      legacy_blob.config.Reset();
      legacy_blob.config.kids_mode = rc_vehicle::KidsModeConfig{};
      legacy_blob.config.mode = DriveMode::Kids;
      legacy_blob.config.ApplyModeDefaults();
      ESP_LOGI(TAG, "Reset legacy Kids config while migrating NVS v8 -> v9");
    }
    legacy_blob.version = kV9StabConfigVersion;
    legacy_err =
        nvs_set_blob(handle, NVS_LEGACY_KEY, &legacy_blob, sizeof(legacy_blob));
    if (legacy_err != ESP_OK) {
      return legacy_err;
    }
    changed = true;
  }

  return changed ? nvs_commit(handle) : ESP_OK;
}

/** Мигрировать v9 Kids-профили на пропорциональный speed limiter. */
esp_err_t MigrateV9Profiles(nvs_handle_t handle) {
  bool changed = false;
  for (uint8_t m = 0; m < kDriveModeCount; ++m) {
    char key[8];
    const DriveMode mode = static_cast<DriveMode>(m);
    ModeKey(mode, key);

    StabConfigBlob blob{};
    size_t size = sizeof(blob);
    esp_err_t err = nvs_get_blob(handle, key, &blob, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
      continue;
    }
    if (err != ESP_OK) {
      return err;
    }
    if (size != sizeof(blob) || blob.version != kV9StabConfigVersion) {
      continue;
    }

    if (mode == DriveMode::Kids) {
      // Сохраняем остальные пользовательские настройки Kids-профиля.
      blob.config.kids_mode.speed_limit_gain = 1.5f;
      ESP_LOGI(TAG,
               "Updated Kids speed limiter gain while migrating v9 -> v10");
    }
    blob.version = kV10StabConfigVersion;
    err = nvs_set_blob(handle, key, &blob, sizeof(blob));
    if (err != ESP_OK) {
      return err;
    }
    changed = true;
  }

  StabConfigBlob legacy_blob{};
  size_t legacy_size = sizeof(legacy_blob);
  esp_err_t legacy_err =
      nvs_get_blob(handle, NVS_LEGACY_KEY, &legacy_blob, &legacy_size);
  if (legacy_err != ESP_ERR_NVS_NOT_FOUND && legacy_err != ESP_OK) {
    return legacy_err;
  }
  if (legacy_err == ESP_OK && legacy_size == sizeof(legacy_blob) &&
      legacy_blob.version == kV9StabConfigVersion) {
    if (legacy_blob.config.mode == DriveMode::Kids) {
      // Сохраняем остальные пользовательские настройки legacy-профиля.
      legacy_blob.config.kids_mode.speed_limit_gain = 1.5f;
      ESP_LOGI(
          TAG,
          "Updated legacy Kids speed limiter gain while migrating v9 -> v10");
    }
    legacy_blob.version = kV10StabConfigVersion;
    legacy_err =
        nvs_set_blob(handle, NVS_LEGACY_KEY, &legacy_blob, sizeof(legacy_blob));
    if (legacy_err != ESP_OK) {
      return legacy_err;
    }
    changed = true;
  }

  return changed ? nvs_commit(handle) : ESP_OK;
}

/** Мигрировать v10 Kids-профили на безопасный yaw-rate пресет. */
esp_err_t MigrateV10Profiles(nvs_handle_t handle) {
  bool changed = false;
  for (uint8_t m = 0; m < kDriveModeCount; ++m) {
    char key[8];
    const DriveMode mode = static_cast<DriveMode>(m);
    ModeKey(mode, key);

    StabConfigBlob blob{};
    size_t size = sizeof(blob);
    esp_err_t err = nvs_get_blob(handle, key, &blob, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) continue;
    if (err != ESP_OK) return err;
    if (size != sizeof(blob) || blob.version != kV10StabConfigVersion) {
      continue;
    }

    if (mode == DriveMode::Kids) {
      ApplyKidsYawSafetyPreset(blob.config);
      ESP_LOGI(TAG, "Updated Kids yaw preset while migrating v10 -> v11");
    }
    blob.version = kV11StabConfigVersion;
    err = nvs_set_blob(handle, key, &blob, sizeof(blob));
    if (err != ESP_OK) return err;
    changed = true;
  }

  StabConfigBlob legacy_blob{};
  size_t legacy_size = sizeof(legacy_blob);
  esp_err_t legacy_err =
      nvs_get_blob(handle, NVS_LEGACY_KEY, &legacy_blob, &legacy_size);
  if (legacy_err != ESP_ERR_NVS_NOT_FOUND && legacy_err != ESP_OK) {
    return legacy_err;
  }
  if (legacy_err == ESP_OK && legacy_size == sizeof(legacy_blob) &&
      legacy_blob.version == kV10StabConfigVersion) {
    if (legacy_blob.config.mode == DriveMode::Kids) {
      ApplyKidsYawSafetyPreset(legacy_blob.config);
      ESP_LOGI(TAG,
               "Updated legacy Kids yaw preset while migrating v10 -> v11");
    }
    legacy_blob.version = kV11StabConfigVersion;
    legacy_err =
        nvs_set_blob(handle, NVS_LEGACY_KEY, &legacy_blob, sizeof(legacy_blob));
    if (legacy_err != ESP_OK) return legacy_err;
    changed = true;
  }

  return changed ? nvs_commit(handle) : ESP_OK;
}

/**
 * @brief Мигрировать v11 профили: взвести KidsModeConfig::limiters_enabled.
 *
 * Поле легло в бывший padding-байт KidsModeConfig, поэтому layout структуры не
 * изменился и все пользовательские настройки (PID, лимиты, тримы) переносятся
 * побайтово. Но содержимое этого байта в старых блобах не определено, поэтому
 * его нужно принудительно взвести в true — иначе защита ребёнка могла бы молча
 * оказаться снятой после обновления прошивки (LOS-286). Флаг взводится во всех
 * слотах: KidsModeConfig присутствует в конфиге каждого режима.
 */
esp_err_t MigrateV11Profiles(nvs_handle_t handle) {
  bool changed = false;
  for (uint8_t m = 0; m < kDriveModeCount; ++m) {
    char key[8];
    ModeKey(static_cast<DriveMode>(m), key);

    StabConfigBlob blob{};
    size_t size = sizeof(blob);
    esp_err_t err = nvs_get_blob(handle, key, &blob, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) continue;
    if (err != ESP_OK) return err;
    if (size != sizeof(blob) || blob.version != kV11StabConfigVersion) {
      continue;
    }

    blob.config.kids_mode.limiters_enabled = true;
    blob.version = kV12StabConfigVersion;
    err = nvs_set_blob(handle, key, &blob, sizeof(blob));
    if (err != ESP_OK) return err;
    changed = true;
  }

  StabConfigBlob legacy_blob{};
  size_t legacy_size = sizeof(legacy_blob);
  esp_err_t legacy_err =
      nvs_get_blob(handle, NVS_LEGACY_KEY, &legacy_blob, &legacy_size);
  if (legacy_err != ESP_ERR_NVS_NOT_FOUND && legacy_err != ESP_OK) {
    return legacy_err;
  }
  if (legacy_err == ESP_OK && legacy_size == sizeof(legacy_blob) &&
      legacy_blob.version == kV11StabConfigVersion) {
    legacy_blob.config.kids_mode.limiters_enabled = true;
    legacy_blob.version = kV12StabConfigVersion;
    legacy_err =
        nvs_set_blob(handle, NVS_LEGACY_KEY, &legacy_blob, sizeof(legacy_blob));
    if (legacy_err != ESP_OK) return legacy_err;
    changed = true;
  }

  if (changed) {
    ESP_LOGI(TAG, "Enabled Kids limiters while migrating v11 -> v12");
    return nvs_commit(handle);
  }
  return ESP_OK;
}

/**
 * @brief Мигрировать v12 профили: новые yaw-коэффициенты Normal (LOS-366).
 *
 * Layout не менялся. Коэффициенты заменяются, только если в профиле Normal
 * остались прежние значения по умолчанию, см.
 * StabilizationConfig::UpgradeLegacyNormalYawGains().
 */
esp_err_t MigrateV12Profiles(nvs_handle_t handle) {
  bool changed = false;
  for (uint8_t m = 0; m < kDriveModeCount; ++m) {
    char key[8];
    ModeKey(static_cast<DriveMode>(m), key);

    StabConfigBlob blob{};
    size_t size = sizeof(blob);
    esp_err_t err = nvs_get_blob(handle, key, &blob, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) continue;
    if (err != ESP_OK) return err;
    if (size != sizeof(blob) || blob.version != kV12StabConfigVersion) {
      continue;
    }

    if (blob.config.UpgradeLegacyNormalYawGains()) {
      ESP_LOGI(TAG, "Updated Normal yaw gains while migrating v12 -> v13");
    }
    blob.version = kV13StabConfigVersion;
    err = nvs_set_blob(handle, key, &blob, sizeof(blob));
    if (err != ESP_OK) return err;
    changed = true;
  }

  StabConfigBlob legacy_blob{};
  size_t legacy_size = sizeof(legacy_blob);
  esp_err_t legacy_err =
      nvs_get_blob(handle, NVS_LEGACY_KEY, &legacy_blob, &legacy_size);
  if (legacy_err != ESP_ERR_NVS_NOT_FOUND && legacy_err != ESP_OK) {
    return legacy_err;
  }
  if (legacy_err == ESP_OK && legacy_size == sizeof(legacy_blob) &&
      legacy_blob.version == kV12StabConfigVersion) {
    if (legacy_blob.config.UpgradeLegacyNormalYawGains()) {
      ESP_LOGI(TAG,
               "Updated legacy Normal yaw gains while migrating v12 -> v13");
    }
    legacy_blob.version = kV13StabConfigVersion;
    legacy_err =
        nvs_set_blob(handle, NVS_LEGACY_KEY, &legacy_blob, sizeof(legacy_blob));
    if (legacy_err != ESP_OK) return legacy_err;
    changed = true;
  }

  return changed ? nvs_commit(handle) : ESP_OK;
}

/**
 * @brief Мигрировать v13 профили: новые yaw-коэффициенты Sport (LOS-371).
 *
 * Layout не менялся. Коэффициенты заменяются, только если в профиле Sport
 * остались прежние значения по умолчанию, см.
 * StabilizationConfig::UpgradeLegacySportYawGains().
 */
esp_err_t MigrateV13Profiles(nvs_handle_t handle) {
  bool changed = false;
  for (uint8_t m = 0; m < kDriveModeCount; ++m) {
    char key[8];
    ModeKey(static_cast<DriveMode>(m), key);

    StabConfigBlob blob{};
    size_t size = sizeof(blob);
    esp_err_t err = nvs_get_blob(handle, key, &blob, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) continue;
    if (err != ESP_OK) return err;
    if (size != sizeof(blob) || blob.version != kV13StabConfigVersion) {
      continue;
    }

    if (blob.config.UpgradeLegacySportYawGains()) {
      ESP_LOGI(TAG, "Updated Sport yaw gains while migrating v13 -> v14");
    }
    blob.version = kV14StabConfigVersion;
    err = nvs_set_blob(handle, key, &blob, sizeof(blob));
    if (err != ESP_OK) return err;
    changed = true;
  }

  StabConfigBlob legacy_blob{};
  size_t legacy_size = sizeof(legacy_blob);
  esp_err_t legacy_err =
      nvs_get_blob(handle, NVS_LEGACY_KEY, &legacy_blob, &legacy_size);
  if (legacy_err != ESP_ERR_NVS_NOT_FOUND && legacy_err != ESP_OK) {
    return legacy_err;
  }
  if (legacy_err == ESP_OK && legacy_size == sizeof(legacy_blob) &&
      legacy_blob.version == kV13StabConfigVersion) {
    if (legacy_blob.config.UpgradeLegacySportYawGains()) {
      ESP_LOGI(TAG,
               "Updated legacy Sport yaw gains while migrating v13 -> v14");
    }
    legacy_blob.version = kV14StabConfigVersion;
    legacy_err =
        nvs_set_blob(handle, NVS_LEGACY_KEY, &legacy_blob, sizeof(legacy_blob));
    if (legacy_err != ESP_OK) return legacy_err;
    changed = true;
  }

  return changed ? nvs_commit(handle) : ESP_OK;
}

/**
 * @brief Мигрировать v14 профили: roll_comp (LOS-386).
 *
 * Расширение раскладки уже сделал MigratePreRollCompLayout(), roll_comp там
 * получил значения по умолчанию; здесь только номер версии.
 */
esp_err_t MigrateV14Profiles(nvs_handle_t handle) {
  bool changed = false;
  auto bump = [&](const char* key) -> esp_err_t {
    StabConfigBlob blob{};
    size_t size = sizeof(blob);
    esp_err_t err = nvs_get_blob(handle, key, &blob, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    if (size != sizeof(blob) || blob.version != kV14StabConfigVersion) {
      return ESP_OK;
    }
    blob.version = kV15StabConfigVersion;
    err = nvs_set_blob(handle, key, &blob, sizeof(blob));
    if (err != ESP_OK) return err;
    changed = true;
    return ESP_OK;
  };

  for (uint8_t m = 0; m < kDriveModeCount; ++m) {
    char key[8];
    ModeKey(static_cast<DriveMode>(m), key);
    const esp_err_t err = bump(key);
    if (err != ESP_OK) return err;
  }
  const esp_err_t err = bump(NVS_LEGACY_KEY);
  if (err != ESP_OK) return err;

  return changed ? nvs_commit(handle) : ESP_OK;
}

/**
 * @brief Мигрировать v15 профили: новый slip-PID Drift (LOS-380).
 *
 * Layout не менялся. Параметры заменяются, только если в профиле Drift
 * остались прежние значения по умолчанию, см.
 * StabilizationConfig::UpgradeLegacyDriftSlipGains().
 */
esp_err_t MigrateV15Profiles(nvs_handle_t handle) {
  bool changed = false;
  auto upgrade = [&](const char* key) -> esp_err_t {
    StabConfigBlob blob{};
    size_t size = sizeof(blob);
    esp_err_t err = nvs_get_blob(handle, key, &blob, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    if (size != sizeof(blob) || blob.version != kV15StabConfigVersion) {
      return ESP_OK;
    }
    if (blob.config.UpgradeLegacyDriftSlipGains()) {
      ESP_LOGI(TAG, "Updated Drift slip gains (%s) while migrating v15 -> v16",
               key);
    }
    blob.version = kCurrentStabConfigVersion;
    err = nvs_set_blob(handle, key, &blob, sizeof(blob));
    if (err != ESP_OK) return err;
    changed = true;
    return ESP_OK;
  };

  for (uint8_t m = 0; m < kDriveModeCount; ++m) {
    char key[8];
    ModeKey(static_cast<DriveMode>(m), key);
    const esp_err_t err = upgrade(key);
    if (err != ESP_OK) return err;
  }
  const esp_err_t err = upgrade(NVS_LEGACY_KEY);
  if (err != ESP_OK) return err;

  return changed ? nvs_commit(handle) : ESP_OK;
}

esp_err_t Load(DriveMode mode, StabilizationConfig& config) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    if (err != ESP_ERR_NVS_NOT_FOUND) {
      ESP_LOGW(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
    }
    return err;
  }

  char key[8];

  err = MigratePreRollCompLayout(handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to migrate NVS config: %s", esp_err_to_name(err));
    nvs_close(handle);
    return err;
  }
  err = MigrateV8Profiles(handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to migrate NVS config: %s", esp_err_to_name(err));
    nvs_close(handle);
    return err;
  }
  err = MigrateV9Profiles(handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to migrate NVS config: %s", esp_err_to_name(err));
    nvs_close(handle);
    return err;
  }
  err = MigrateV10Profiles(handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to migrate NVS config: %s", esp_err_to_name(err));
    nvs_close(handle);
    return err;
  }
  err = MigrateV11Profiles(handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to migrate NVS config: %s", esp_err_to_name(err));
    nvs_close(handle);
    return err;
  }
  err = MigrateV12Profiles(handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to migrate NVS config: %s", esp_err_to_name(err));
    nvs_close(handle);
    return err;
  }
  err = MigrateV13Profiles(handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to migrate NVS config: %s", esp_err_to_name(err));
    nvs_close(handle);
    return err;
  }
  err = MigrateV14Profiles(handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to migrate NVS config: %s", esp_err_to_name(err));
    nvs_close(handle);
    return err;
  }
  err = MigrateV15Profiles(handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to migrate NVS config: %s", esp_err_to_name(err));
    nvs_close(handle);
    return err;
  }
  ModeKey(mode, key);
  err = ReadBlobKey(handle, key, config);
  nvs_close(handle);

  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Loaded stabilization config for mode=%u: slew_steering=%.2f",
             static_cast<unsigned>(mode), config.slew_steering);
  } else if (err != ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGW(TAG, "Failed to read mode=%u config: %s",
             static_cast<unsigned>(mode), esp_err_to_name(err));
  }
  return err;
}

esp_err_t Load(StabilizationConfig& config) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    if (err != ESP_ERR_NVS_NOT_FOUND) {
      ESP_LOGW(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
    }
    return err;
  }

  // Активный режим → его слот.

  err = MigratePreRollCompLayout(handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to migrate NVS config: %s", esp_err_to_name(err));
    nvs_close(handle);
    return err;
  }
  err = MigrateV8Profiles(handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to migrate NVS config: %s", esp_err_to_name(err));
    nvs_close(handle);
    return err;
  }
  err = MigrateV9Profiles(handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to migrate NVS config: %s", esp_err_to_name(err));
    nvs_close(handle);
    return err;
  }
  err = MigrateV10Profiles(handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to migrate NVS config: %s", esp_err_to_name(err));
    nvs_close(handle);
    return err;
  }
  err = MigrateV11Profiles(handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to migrate NVS config: %s", esp_err_to_name(err));
    nvs_close(handle);
    return err;
  }
  err = MigrateV12Profiles(handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to migrate NVS config: %s", esp_err_to_name(err));
    nvs_close(handle);
    return err;
  }
  err = MigrateV13Profiles(handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to migrate NVS config: %s", esp_err_to_name(err));
    nvs_close(handle);
    return err;
  }
  err = MigrateV14Profiles(handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to migrate NVS config: %s", esp_err_to_name(err));
    nvs_close(handle);
    return err;
  }
  err = MigrateV15Profiles(handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to migrate NVS config: %s", esp_err_to_name(err));
    nvs_close(handle);
    return err;
  }
  uint8_t active = 0;
  esp_err_t active_err = nvs_get_u8(handle, NVS_ACTIVE_KEY, &active);
  if (active_err == ESP_OK && active < kDriveModeCount) {
    char key[8];
    ModeKey(static_cast<DriveMode>(active), key);
    err = ReadBlobKey(handle, key, config);
    if (err == ESP_OK) {
      nvs_close(handle);
      ESP_LOGI(TAG, "Loaded active stabilization config: mode=%u", active);
      return ESP_OK;
    }
  }

  // Миграция: старый однослотовый ключ "config".
  err = ReadBlobKey(handle, NVS_LEGACY_KEY, config);
  nvs_close(handle);
  if (err == ESP_OK) {
    ESP_LOGW(TAG, "Migrated legacy single-slot config (mode=%u)",
             static_cast<unsigned>(config.mode));
    // Перенесём в per-mode слот, чтобы старый ключ больше не использовался.
    Save(config);
    return ESP_OK;
  }

  return ESP_ERR_NVS_NOT_FOUND;
}

esp_err_t Save(const StabilizationConfig& config) {
  if (!config.IsValid()) {
    ESP_LOGE(TAG, "Cannot save invalid config");
    return ESP_ERR_INVALID_ARG;
  }

  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open NVS namespace for write: %s",
             esp_err_to_name(err));
    return err;
  }

  StabConfigBlob blob{};
  blob.version = kCurrentStabConfigVersion;
  blob.config = config;

  char key[8];
  ModeKey(config.mode, key);
  err = nvs_set_blob(handle, key, &blob, sizeof(StabConfigBlob));
  if (err == ESP_OK) {
    err = nvs_set_u8(handle, NVS_ACTIVE_KEY, static_cast<uint8_t>(config.mode));
  }
  if (err == ESP_OK) {
    err = nvs_commit(handle);
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "Saved stabilization config: mode=%u slew_steering=%.2f",
               static_cast<unsigned>(config.mode), config.slew_steering);
    } else {
      ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
    }
  } else {
    ESP_LOGE(TAG, "Failed to write config to NVS: %s", esp_err_to_name(err));
  }

  nvs_close(handle);
  return err;
}

esp_err_t Erase() {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to open NVS namespace for erase: %s",
             esp_err_to_name(err));
    return err;
  }

  // Удаляем все per-mode слоты, активный режим и legacy-ключ. Отсутствующие
  // ключи (ESP_ERR_NVS_NOT_FOUND) не считаем ошибкой.
  for (uint8_t m = 0; m < kDriveModeCount; ++m) {
    char key[8];
    ModeKey(static_cast<DriveMode>(m), key);
    esp_err_t e = nvs_erase_key(handle, key);
    if (e != ESP_OK && e != ESP_ERR_NVS_NOT_FOUND) err = e;
  }
  {
    esp_err_t e = nvs_erase_key(handle, NVS_ACTIVE_KEY);
    if (e != ESP_OK && e != ESP_ERR_NVS_NOT_FOUND) err = e;
  }
  {
    esp_err_t e = nvs_erase_key(handle, NVS_LEGACY_KEY);
    if (e != ESP_OK && e != ESP_ERR_NVS_NOT_FOUND) err = e;
  }

  if (err == ESP_OK) {
    err = nvs_commit(handle);
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "Erased all stabilization configs from NVS");
    }
  } else {
    ESP_LOGW(TAG, "Failed to erase configs: %s", esp_err_to_name(err));
  }

  nvs_close(handle);
  return err;
}

}  // namespace stab_config_nvs
