#include "stabilization_manager.hpp"

#include <algorithm>
#include <cstdio>
#include <mutex>

#include "log_format.hpp"
#include "slew_rate.hpp"

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
    return false;
  }

  // Читаем текущий mode под локом для корректного сравнения
  DriveMode current_mode;
  {
    std::lock_guard<std::mutex> lock(config_mutex_);
    current_mode = config_.mode;
  }

  // При смене режима автоматически применить предустановки PID для нового
  // режима
  if (validated_config.mode != current_mode) {
    validated_config.ApplyModeDefaults();
    // Сброс ПИД при смене режима — очищает интегратор предыдущего режима,
    // предотвращая рывок при переходе (особенно при переходе в/из drift mode)
    yaw_ctrl_.Reset();
    slip_ctrl_.Reset();
    mode_transition_weight_ = 0.0f;  // Запустить плавный переход
    {
      LogFormat fmt;
      fmt << "Mode changed to " << static_cast<unsigned>(validated_config.mode)
          << ", defaults applied";
      platform_.Log(LogLevel::Info, fmt.str());
    }
  }

  // Применить к фильтрам
  madgwick_.SetBeta(validated_config.filter.madgwick_beta);

  // Применить к LPF (если IMU включен)
  if (imu_handler_) {
    imu_handler_->SetLpfCutoff(validated_config.filter.lpf_cutoff_hz);
  }

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

  // Сохранить конфигурацию под локом
  {
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_ = validated_config;
  }

  if (save_to_nvs) {
    auto result = platform_.SaveStabilizationConfig(validated_config);
    if (IsOk(result)) {
      platform_.Log(LogLevel::Info, "Stabilization config saved to NVS");
    } else {
      platform_.Log(LogLevel::Warning,
                    "Failed to save stabilization config to NVS");
      return false;
    }
  }

  return true;
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

  // Применить конфигурацию к фильтрам
  madgwick_.SetBeta(cfg.filter.madgwick_beta);

  // Применить к LPF (если IMU включен)
  if (imu_handler_) {
    imu_handler_->SetLpfCutoff(cfg.filter.lpf_cutoff_hz);
  }
}

void StabilizationManager::UpdateWeights(uint32_t dt_ms) {
  if (dt_ms == 0) {
    return;
  }

  StabilizationConfig cfg;
  {
    std::lock_guard<std::mutex> lock(config_mutex_);
    cfg = config_;
  }

  // ─────────────────────────────────────────────────────────────────────
  // Плавное нарастание/спад веса стабилизации
  // ─────────────────────────────────────────────────────────────────────

  const float target_weight = cfg.enabled ? 1.0f : 0.0f;
  if (cfg.fade_ms == 0) {
    stab_weight_ = target_weight;
  } else {
    const float fade_rate_per_sec =
        1000.0f / static_cast<float>(cfg.fade_ms);
    stab_weight_ =
        ApplySlewRate(target_weight, stab_weight_, fade_rate_per_sec, dt_ms);
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
      mode_transition_weight_ =
          ApplySlewRate(1.0f, mode_transition_weight_, fade_rate, dt_ms);
    }
  }
}

void StabilizationManager::ResetWeights() {
  stab_weight_ = 0.0f;
  mode_transition_weight_ = 1.0f;
}

}  // namespace rc_vehicle