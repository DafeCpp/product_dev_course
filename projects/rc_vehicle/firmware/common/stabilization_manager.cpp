#include "stabilization_manager.hpp"

#include <algorithm>
#include <cstdio>
#include <firmware_common/slew_rate.hpp>
#include <mutex>

#include "drive_mode_registry.hpp"
#include "esp_log.h"
#include "log_format.hpp"
#include "telemetry_config_snapshot.hpp"

namespace rc_vehicle {

StabilizationManager::StabilizationManager(VehicleControlPlatform& platform,
                                           MadgwickFilter& madgwick,
                                           YawRateController& yaw_ctrl,
                                           SlipAngleController& slip_ctrl,
                                           ImuHandler* imu_handler)
    : platform_(platform),
      madgwick_(madgwick),
      yaw_ctrl_(yaw_ctrl),
      slip_ctrl_(slip_ctrl),
      imu_handler_(imu_handler) {}

StabilizationConfig StabilizationManager::GetConfig() const {
  std::lock_guard<std::mutex> lock(config_mutex_);
  return config_;
}

bool StabilizationManager::SetConfig(const StabilizationConfig& config,
                                     bool save_to_nvs) {
  // Валидация и ограничение параметров
  StabilizationConfig validated_config = config;
  validated_config.Clamp();

  if (!validated_config.IsValid()) {
    platform_.Log(LogLevel::Error, "Invalid stabilization config");
    // Detailed validation logging
    ESP_LOGE("stab_mgr", "magic=0x%08X (expected 0x%08X)",
             validated_config.magic, kStabilizationConfigMagic);
    ESP_LOGE("stab_mgr", "filter.valid=%d yaw.valid=%d slip.valid=%d",
             validated_config.filter.IsValid(),
             validated_config.yaw_rate.IsValid(),
             validated_config.slip_angle.IsValid());
    ESP_LOGE("stab_mgr",
             "yaw.kp=%.3f ki=%.3f kd=%.4f max_corr=%.3f max_int=%.3f",
             validated_config.yaw_rate.pid.kp, validated_config.yaw_rate.pid.ki,
             validated_config.yaw_rate.pid.kd,
             validated_config.yaw_rate.pid.max_correction,
             validated_config.yaw_rate.pid.max_integral);
    return false;
  }

  // Determine whether this is a mode switch while holding the publication
  // lock, then load a target profile outside it. Recheck after the load: a
  // concurrent update may have changed the active mode meanwhile.
  std::unique_lock<std::mutex> lock(config_mutex_);
  DriveMode current_mode = config_.mode;
  const bool requested_mode_change = validated_config.mode != current_mode;
  std::optional<StabilizationConfig> saved_target;
  while (validated_config.mode != current_mode) {
    lock.unlock();
    saved_target = platform_.LoadStabilizationConfig(validated_config.mode);
    lock.lock();
    if (config_.mode == current_mode) break;
    current_mode = config_.mode;
  }

  // A concurrent request may already have completed this mode switch. The
  // fields in this request then still belong to the old mode and must not
  // overwrite the profile that has just been published.
  if (requested_mode_change && validated_config.mode == current_mode) {
    return true;
  }

  // Apply configuration, publish it and write its snapshot as one serialized
  // transition. The potentially slow NVS write below remains outside it.

  // При смене режима восстановить ранее сохранённую настройку нового режима
  // (per-mode persistence): кастомизация режима не должна теряться при
  // переключении туда-обратно. Если для режима ничего не сохранено — применить
  // хардкод-дефолты режима. Поля, пришедшие в запросе вместе со сменой mode,
  // относятся к СТАРОМУ режиму и игнорируются: профиль режима задаётся его
  // сохранённой конфигурацией, а не «довеском» к переключению.
  if (validated_config.mode != current_mode) {
    const DriveMode target_mode = validated_config.mode;
    bool restored = false;
    if (saved_target.has_value()) {
      validated_config = *saved_target;
      validated_config.mode = target_mode;  // на всякий случай
      validated_config.Clamp();
      restored = validated_config.IsValid();
    }
    if (!restored) {
      validated_config.ApplyModeDefaults();
    }
    // Сброс ПИД при смене режима — очищает интегратор предыдущего режима,
    // предотвращая рывок при переходе (особенно при переходе в/из drift mode)
    yaw_ctrl_.Reset();
    slip_ctrl_.Reset();
    mode_transition_weight_ = 0.0f;  // Запустить плавный переход
    {
      LogFormat fmt;
      fmt << "Mode changed: " << DriveModeRegistry::Get(current_mode).GetName()
          << " -> " << DriveModeRegistry::Get(target_mode).GetName() << ", "
          << (restored ? "saved profile restored" : "defaults applied")
          << ", PID reset";
      platform_.Log(LogLevel::Info, fmt.str());
    }
  }

  ApplyToFilters(validated_config);

  // Обновить коэффициенты ПИД yaw rate и slip angle
  yaw_ctrl_.SetGains(validated_config);
  slip_ctrl_.SetGains(validated_config);

  // Сброс ПИД при мгновенном отключении (fade_ms == 0).
  // При плавном fade сброс произойдёт в control loop когда stab_weight_ → 0.
  if (!validated_config.enabled && validated_config.fade_ms == 0) {
    yaw_ctrl_.Reset();
    slip_ctrl_.Reset();
    stab_weight_ = 0.0f;
  }

  config_ = validated_config;
  // Configuration is live before the potentially slow NVS write. Record the
  // transition now so frames during persistence and failed saves remain
  // attributable to the configuration that was actually applied.
  if (config_snapshot_log_) {
    config_snapshot_log_->Push(TelemetryConfigSnapshot::FromConfig(
        platform_.GetTimeMs(), validated_config));
  }

  lock.unlock();
  if (save_to_nvs) {
    auto result = platform_.SaveStabilizationConfig(validated_config);
    if (result.has_value()) {
      platform_.Log(LogLevel::Info, "Stabilization config saved to NVS");
    } else {
      platform_.Log(LogLevel::Warning,
                    "Failed to save stabilization config to NVS");
      return false;
    }
  }

  return true;
}

void StabilizationManager::ClearAndSeedConfigSnapshots(uint32_t ts_ms) {
  std::lock_guard<std::mutex> lock(config_mutex_);
  if (!config_snapshot_log_) return;
  config_snapshot_log_->Clear();
  config_snapshot_log_->Push(
      TelemetryConfigSnapshot::FromConfig(ts_ms, config_));
}

bool StabilizationManager::LoadFromNvs() {
  auto stab_cfg = platform_.LoadStabilizationConfig();
  if (stab_cfg) {
    {
      std::lock_guard<std::mutex> lock(config_mutex_);
      config_ = *stab_cfg;
    }
    platform_.Log(LogLevel::Info, "Stabilization config loaded from NVS");
    return true;
  } else {
    {
      std::lock_guard<std::mutex> lock(config_mutex_);
      config_.Reset();
    }
    platform_.Log(LogLevel::Info, "Using default stabilization config");
    return false;
  }
}

void StabilizationManager::ApplyConfig() {
  StabilizationConfig cfg;
  {
    std::lock_guard<std::mutex> lock(config_mutex_);
    cfg = config_;
  }
  ApplyToFilters(cfg);
}

void StabilizationManager::ApplyToFilters(const StabilizationConfig& cfg) {
  madgwick_.SetBeta(cfg.filter.madgwick_beta);
  madgwick_.SetAdaptiveBeta(cfg.filter.adaptive_beta_enabled,
                            cfg.filter.adaptive_accel_threshold_g);

  // LPF и Madgwick enable — только если IMU есть
  if (imu_handler_) {
    imu_handler_->SetLpfCutoff(cfg.filter.lpf_cutoff_hz);
    imu_handler_->SetMadgwickEnabled(cfg.filter.madgwick_enabled);
  }
}

void StabilizationManager::UpdateWeights(const StabilizationConfig& cfg,
                                         uint32_t dt_ms) {
  if (dt_ms == 0) {
    return;
  }

  // ─────────────────────────────────────────────────────────────────────
  // Плавное нарастание/спад веса стабилизации
  // ─────────────────────────────────────────────────────────────────────

  const float target_weight = cfg.enabled ? 1.0f : 0.0f;
  if (cfg.fade_ms == 0) {
    stab_weight_ = target_weight;
  } else {
    const float fade_rate_per_sec = 1000.0f / static_cast<float>(cfg.fade_ms);
    stab_weight_ = firmware_common::ApplySlewRate(
        target_weight, stab_weight_, fade_rate_per_sec, dt_ms / 1000.0f);
  }

  // Сброс ПИД при полном отключении — убирает накопленный интегратор
  if (stab_weight_ == 0.0f) {
    yaw_ctrl_.Reset();
    slip_ctrl_.Reset();
  }

  // ─────────────────────────────────────────────────────────────────────
  // Плавный переход между режимами (mode transition fade)
  // mode_transition_weight_ сбрасывается в 0 при смене режима и
  // нарастает к 1 за fade_ms, убирая рывок при переключении normal↔drift.
  // ─────────────────────────────────────────────────────────────────────

  if (mode_transition_weight_ < 1.0f) {
    if (cfg.fade_ms == 0) {
      mode_transition_weight_ = 1.0f;
    } else {
      const float fade_rate = 1000.0f / static_cast<float>(cfg.fade_ms);
      mode_transition_weight_ = firmware_common::ApplySlewRate(
          1.0f, mode_transition_weight_, fade_rate, dt_ms / 1000.0f);
    }
  }
}

void StabilizationManager::ResetWeights() {
  stab_weight_ = 0.0f;
  mode_transition_weight_ = 1.0f;
}

}  // namespace rc_vehicle
