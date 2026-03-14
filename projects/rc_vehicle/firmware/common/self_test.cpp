#include "self_test.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>

namespace rc_vehicle {

std::vector<SelfTestItem> SelfTest::Run(const SelfTestInput& input) {
  std::vector<SelfTestItem> results;
  results.reserve(10);

  char buf[48];

  // 1. Control loop frequency: 490..510 Hz
  {
    std::snprintf(buf, sizeof(buf), "%u Hz", input.loop_hz);
    bool ok = input.loop_hz >= 490 && input.loop_hz <= 510;
    results.emplace_back("control_loop", ok, buf);
  }

  // 2. IMU available
  {
    results.emplace_back("imu_available", input.imu_enabled,
                         input.imu_enabled ? "enabled" : "disabled");
  }

  // 3. Gyro stable at rest: |gyro_xyz| < 5 dps each axis
  {
    float max_gyro =
        std::max({std::abs(input.gyro_x_dps), std::abs(input.gyro_y_dps),
                  std::abs(input.gyro_z_dps)});
    std::snprintf(buf, sizeof(buf), "max %.1f dps", max_gyro);
    results.emplace_back("gyro_stable", max_gyro < 5.0f, buf);
  }

  // 4. Accelerometer ~1g: |accel| in [0.9, 1.1] g
  {
    float accel_mag = std::sqrt(input.accel_x_g * input.accel_x_g +
                                input.accel_y_g * input.accel_y_g +
                                input.accel_z_g * input.accel_z_g);
    std::snprintf(buf, sizeof(buf), "%.3f g", accel_mag);
    bool ok = accel_mag >= 0.9f && accel_mag <= 1.1f;
    results.emplace_back("accel_1g", ok, buf);
  }

  // 5. Madgwick converged: |pitch| < 5°, roll near 0° or ±180° (upside-down mount)
  {
    float abs_pitch = std::abs(input.pitch_deg);
    float abs_roll = std::abs(input.roll_deg);
    // Allow roll ≈ 0° (normal) or ≈ ±180° (IMU mounted upside-down)
    float roll_from_level = std::min(abs_roll, std::abs(180.0f - abs_roll));
    float max_tilt = std::max(abs_pitch, roll_from_level);
    std::snprintf(buf, sizeof(buf), "P=%.1f R=%.1f deg", input.pitch_deg,
                  input.roll_deg);
    results.emplace_back("madgwick_level", max_tilt < 5.0f, buf);
  }

  // 6. EKF at rest (ZUPT): |vx| < 0.05, |vy| < 0.05 m/s
  {
    float max_v = std::max(std::abs(input.ekf_vx), std::abs(input.ekf_vy));
    std::snprintf(buf, sizeof(buf), "vx=%.3f vy=%.3f m/s", input.ekf_vx,
                  input.ekf_vy);
    results.emplace_back("ekf_zupt", max_v < 0.05f, buf);
  }

  // 7. Failsafe not active
  {
    results.emplace_back("failsafe_inactive", !input.failsafe_active,
                         input.failsafe_active ? "ACTIVE" : "inactive");
  }

  // 8. Calibration valid
  {
    results.emplace_back("calib_valid", input.calib_valid,
                         input.calib_valid ? "valid" : "invalid");
  }

  // 9. TelemetryLog operational
  {
    std::snprintf(buf, sizeof(buf), "cap=%zu", input.log_capacity);
    results.emplace_back("telemetry_log", input.log_capacity > 0, buf);
  }

  // 10. PWM operational
  {
    results.emplace_back("pwm_ok", input.pwm_status == 0,
                         input.pwm_status == 0 ? "ok" : "error");
  }

  return results;
}

bool SelfTest::AllPassed(const std::vector<SelfTestItem>& results) {
  for (const auto& item : results) {
    if (!item.passed) return false;
  }
  return true;
}

}  // namespace rc_vehicle
