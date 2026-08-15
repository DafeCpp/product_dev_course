#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <firmware_common/slew_rate.hpp>

#include "auto_drive_coordinator.hpp"
#include "config.hpp"
#include "control_components.hpp"
#include "imu_calibration.hpp"
#include "self_test.hpp"
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
inline void UpdatePwmWithSlewRate(
    VehicleControlPlatform& platform, uint32_t now_ms, float commanded_throttle,
    float commanded_steering, float& applied_throttle, float& applied_steering,
    uint32_t& last_pwm_update, float throttle_trim, float steering_trim,
    float slew_throttle_per_sec, float slew_steering_per_sec) {
  if (now_ms - last_pwm_update >= config::PwmConfig::kUpdateIntervalMs) {
    const uint32_t pwm_dt_ms = now_ms - last_pwm_update;
    last_pwm_update = now_ms;

    applied_throttle = firmware_common::ApplySlewRate(
        commanded_throttle, applied_throttle, slew_throttle_per_sec,
        pwm_dt_ms / 1000.0f);
    applied_steering = firmware_common::ApplySlewRate(
        commanded_steering, applied_steering, slew_steering_per_sec,
        pwm_dt_ms / 1000.0f);

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
inline SensorSnapshot BuildSensorSnapshot(
    const RcInputHandler* rc_handler, const WifiCommandHandler* wifi_handler,
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
    s.mag_enabled = imu_handler->IsMagEnabled();
    s.mag_rejected = imu_handler->IsMagRejected();
    s.mag_gate_active = imu_handler->IsMagGateActive();
    s.mag_norm_mgauss = imu_handler->GetMagNormMGauss();
    s.expected_mag_norm_mgauss = imu_handler->GetExpectedMagNormMGauss();
    s.mag_sample_sequence = imu_handler->GetMagSampleSequence();
    if (s.mag_enabled) {
      s.mag_data = imu_handler->GetMagData();
      s.heading_deg = imu_handler->GetHeadingDeg();
      s.heading_rel_deg = imu_handler->GetRelativeHeadingDeg();
    }
  }
  return s;
}

// ═════════════════════════════════════════════════════════════════════════
// BuildAutoDriveInput
// ═════════════════════════════════════════════════════════════════════════

/** Построить входные данные для авто-процедур из снимка датчиков. */
inline AutoDriveInput BuildAutoDriveInput(const SensorSnapshot& sensors,
                                          const ImuCalibration& imu_calib,
                                          uint32_t dt_ms, uint32_t now_ms = 0) {
  AutoDriveInput ad;
  ad.rc_active = sensors.rc_active;
  ad.imu_enabled = sensors.imu_enabled;
  ad.dt_sec = static_cast<float>(dt_ms) * 0.001f;
  ad.ts_ms = now_ms;
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
// ComputeForwardAccelG
// ═════════════════════════════════════════════════════════════════════════

/**
 * Продольное ЛИНЕЙНОЕ ускорение [g] со снятием гравитации по ТЕКУЩЕЙ
 * ориентации (LOS-245). Положительное = ускорение вперёд.
 *
 * ImuCalibration::GetForwardAccel() вычитает КОНСТАНТНЫЙ RestDownVec(), то
 * есть неявно считает машину горизонтальной, и при реальном тангаже отдаёт
 * наклон вместо ускорения: в ось «вперёд» протекает sin(pitch)·g. По логу
 * ночного заезда 25.07 (LOS-244) утечка имела медиану 0.021 g и p95 0.253 g
 * против медианы 0.010 g полезного сигнала — то есть вдвое превышала его, и
 * accel-лимитер Kids Mode (порог 0.15 g подделывается тангажом всего в 8.6°)
 * срезал газ на кочках и клевках вместо разгона.
 *
 * Здесь гравитация снимается так же, как в VehicleEkf::UpdateFromImu()
 * (grav_x = −sin(pitch), см. vehicle_ekf.cpp): это тот же фикс, что уже
 * сделан для EKF в LOS-232/LOS-240, распространённый на этого потребителя.
 *
 * ОГРАНИЧЕНИЕ: оценщик тангажа не отличает УСТОЙЧИВОЕ продольное ускорение от
 * наклона и с постоянной времени 1/corr_gain_hz уводит его в тангаж — разгон
 * дольше нескольких секунд перестаёт быть виден. Годится для коротких
 * событий (тычок газом), не годится как измеритель длительного разгона.
 *
 * @param veh_imu     accel ПОСЛЕ Apply() и RotateToVehicleFrame() — ax уже
 *                    есть ось «вперёд». ДОЛЖЕН быть получен ровно из
 *                    sensor_imu: инвариант компилятором не проверяется, и
 *                    рассогласованная пара молча даст неверный результат.
 * @param sensor_imu  те же данные ДО ротации — нужны только для фолбэка.
 * @param tilt_valid  есть ли ДОВЕРЕННАЯ оценка тангажа (только TiltEstimator;
 *                    Madgwick сюда подавать нельзя — он сам заваливается на
 *                    разгоне, см. control_loop_processor.cpp). false →
 *                    деградация до GetForwardAccel().
 *
 * На горизонтали основной путь и фолбэк дают одно и то же, ПОКА
 * accel_forward_vec ортогонален gravity_vec: SetForwardDirection() это
 * гарантирует, но SetData() (загрузка блоба из NVS) только нормализует, не
 * ортогонализуя. На неортогональном блобе veh_imu.ax (после
 * OrthogonalizeForward) и GetForwardAccel() разойдутся; корректнее первое.
 */
inline float ComputeForwardAccelG(const ImuCalibration& imu_calib,
                                  const ImuData& veh_imu,
                                  const ImuData& sensor_imu, float pitch_rad,
                                  bool tilt_valid) {
  if (!tilt_valid) return imu_calib.GetForwardAccel(sensor_imu);
  return veh_imu.ax + std::sin(pitch_rad);
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
