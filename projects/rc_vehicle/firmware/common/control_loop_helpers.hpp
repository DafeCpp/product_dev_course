#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>

#include "auto_drive_coordinator.hpp"
#include "config.hpp"
#include "control_components.hpp"
#include "imu_calibration.hpp"
#include "self_test.hpp"
#include "slew_rate.hpp"
#include "stabilization_manager.hpp"
#include "telemetry_manager.hpp"
#include "vehicle_control_platform.hpp"
#include "vehicle_ekf.hpp"

namespace rc_vehicle {

// ═════════════════════════════════════════════════════════════════════════
// SelectControlSource
// ═════════════════════════════════════════════════════════════════════════

/** Выбор источника управления (RC приоритетнее Wi-Fi). */
inline bool SelectControlSource(const SensorSnapshot& sensors,
                                float& commanded_throttle,
                                float& commanded_steering) {
  if (sensors.rc_active && sensors.rc_cmd) {
    commanded_throttle = sensors.rc_cmd->throttle;
    commanded_steering = sensors.rc_cmd->steering;
    return true;
  }
  if (sensors.wifi_active && sensors.wifi_cmd) {
    commanded_throttle = sensors.wifi_cmd->throttle;
    commanded_steering = sensors.wifi_cmd->steering;
    return true;
  }
  return false;
}

// ═════════════════════════════════════════════════════════════════════════
// UpdatePwmWithSlewRate
// ═════════════════════════════════════════════════════════════════════════

/** Обновление PWM с ограничением скорости изменения (slew rate). */
inline void UpdatePwmWithSlewRate(VehicleControlPlatform& platform,
                                  uint32_t now_ms, float commanded_throttle,
                                  float commanded_steering,
                                  float& applied_throttle,
                                  float& applied_steering,
                                  uint32_t& last_pwm_update,
                                  float throttle_trim, float steering_trim,
                                  float slew_throttle_per_sec,
                                  float slew_steering_per_sec) {
  if (now_ms - last_pwm_update >= config::PwmConfig::kUpdateIntervalMs) {
    const uint32_t pwm_dt_ms = now_ms - last_pwm_update;
    last_pwm_update = now_ms;

    applied_throttle = ApplySlewRate(commanded_throttle, applied_throttle,
                                     slew_throttle_per_sec, pwm_dt_ms);
    applied_steering = ApplySlewRate(commanded_steering, applied_steering,
                                     slew_steering_per_sec, pwm_dt_ms);

    platform.SetPwm(applied_throttle + throttle_trim,
                    applied_steering + steering_trim);
  }
}

// ═════════════════════════════════════════════════════════════════════════
// HandleAutoDriveCompletion
// ═════════════════════════════════════════════════════════════════════════

/** Применить результаты завершённых авто-процедур (trim, CoM offset). */
void HandleAutoDriveCompletion(const AutoDriveOutput& ad_out,
                               StabilizationManager* stab_mgr,
                               ImuCalibration& imu_calib,
                               VehicleControlPlatform& platform);

// ═════════════════════════════════════════════════════════════════════════
// BuildSelfTestInput
// ═════════════════════════════════════════════════════════════════════════

/** Ссылки на подсистемы, нужные для self-test. */
struct SelfTestContext {
  const std::atomic<uint32_t>& last_loop_hz;
  const ImuHandler* imu_handler;
  const MadgwickFilter& madgwick;
  const VehicleEkf& ekf;
  const RcInputHandler* rc_handler;
  const WifiCommandHandler* wifi_handler;
  const ImuCalibration& imu_calib;
  const TelemetryManager* telem_mgr;
  bool platform_exists;
  bool inited;
};

/** Построить SelfTestInput из текущего состояния подсистем. */
SelfTestInput BuildSelfTestInput(const SelfTestContext& ctx);

// ═════════════════════════════════════════════════════════════════════════
// BuildSensorSnapshot
// ═════════════════════════════════════════════════════════════════════════

/** Построить атомарный снимок состояния датчиков. */
inline SensorSnapshot BuildSensorSnapshot(const RcInputHandler* rc_handler,
                                          const WifiCommandHandler* wifi_handler,
                                          const ImuHandler* imu_handler) {
  SensorSnapshot s;
  s.rc_active = rc_handler && rc_handler->IsActive();
  if (s.rc_active) {
    s.rc_cmd = rc_handler->GetCommand();
  }
  s.wifi_active = wifi_handler && wifi_handler->IsActive();
  if (s.wifi_active) {
    s.wifi_cmd = wifi_handler->GetCommand();
  }
  s.imu_enabled = imu_handler && imu_handler->IsEnabled();
  if (s.imu_enabled) {
    s.imu_data = imu_handler->GetData();
    s.filtered_gz = imu_handler->GetFilteredGyroZ();
  }
  return s;
}

// ═════════════════════════════════════════════════════════════════════════
// BuildAutoDriveInput
// ═════════════════════════════════════════════════════════════════════════

/** Построить входные данные для авто-процедур из снимка датчиков. */
inline AutoDriveInput BuildAutoDriveInput(const SensorSnapshot& sensors,
                                          const ImuCalibration& imu_calib,
                                          uint32_t dt_ms) {
  AutoDriveInput ad;
  ad.rc_active = sensors.rc_active;
  ad.imu_enabled = sensors.imu_enabled;
  ad.dt_sec = static_cast<float>(dt_ms) * 0.001f;
  if (sensors.imu_enabled) {
    ad.fwd_accel = imu_calib.GetForwardAccel(sensors.imu_data);
    ad.accel_mag = std::sqrt(sensors.imu_data.ax * sensors.imu_data.ax +
                             sensors.imu_data.ay * sensors.imu_data.ay +
                             sensors.imu_data.az * sensors.imu_data.az);
    ad.cal_ax = sensors.imu_data.ax;
    ad.cal_ay = sensors.imu_data.ay;
    ad.gyro_z = sensors.filtered_gz;
  }
  return ad;
}

// ═════════════════════════════════════════════════════════════════════════
// CorrectImuForComOffset
// ═════════════════════════════════════════════════════════════════════════

/** Коррекция акселерометра за смещение IMU от центра масс.
 *  Возвращает обновлённое prev_gz_rad_s. */
inline float CorrectImuForComOffset(SensorSnapshot& sensors,
                                    ImuCalibration& imu_calib,
                                    float prev_gz_rad_s, uint32_t dt_ms) {
  if (!sensors.imu_enabled || dt_ms == 0) return prev_gz_rad_s;

  constexpr float kDeg2Rad = 3.14159265358979f / 180.0f;
  const float dt_sec = static_cast<float>(dt_ms) * 0.001f;
  const float gz_rad = sensors.filtered_gz * kDeg2Rad;
  const float alpha_rad = (gz_rad - prev_gz_rad_s) / dt_sec;
  imu_calib.CorrectForComOffset(sensors.imu_data, gz_rad, alpha_rad);
  return gz_rad;
}

}  // namespace rc_vehicle
