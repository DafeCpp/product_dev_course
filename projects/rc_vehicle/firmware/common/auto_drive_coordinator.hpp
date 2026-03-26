#pragma once

#include "com_offset_calibration.hpp"
#include "speed_calibration.hpp"
#include "steering_trim_calibration.hpp"
#include "test_runner.hpp"

namespace rc_vehicle {

class CalibrationManager;

/** Входные данные от датчиков для авто-процедур. */
struct AutoDriveInput {
  float fwd_accel{0.0f};
  float accel_mag{1.0f};
  float gyro_z{0.0f};
  float cal_ax{0.0f};
  float cal_ay{0.0f};
  float dt_sec{0.0f};
  float speed_ms{0.0f};  ///< EKF speed [m/s] (0 если IMU недоступен)
  bool rc_active{false};
  bool imu_enabled{false};
};

/** Результат одной итерации авто-процедур. */
struct AutoDriveOutput {
  bool active{false};
  float throttle{0.0f};
  float steering{0.0f};

  // Однократные события завершения (валидны только на тике завершения)
  bool trim_completed{false};
  SteeringTrimCalibration::Result trim_result{};

  bool com_completed{false};
  ComOffsetCalibration::Result com_result{};

  bool speed_cal_completed{false};
  SpeedCalibration::Result speed_cal_result{};
};

/**
 * @brief Координатор авто-процедур (trim calib, CoM calib, test runner,
 *        auto-forward calibration).
 *
 * Обеспечивает взаимное исключение: только одна процедура может быть
 * активна одновременно. Извлечён из VehicleControlUnified для SRP.
 */
class AutoDriveCoordinator {
 public:
  AutoDriveCoordinator() = default;

  void SetCalibrationManager(CalibrationManager* mgr) { calib_mgr_ = mgr; }

  /** Обновить все активные процедуры. Вызывается каждую итерацию loop. */
  AutoDriveOutput Update(const AutoDriveInput& input);

  /** true если любая авто-процедура активна. */
  [[nodiscard]] bool IsAnyActive() const;

  // ── Steering Trim Calibration ────────────────────────────────────────
  bool StartTrimCalib(float target_accel_g, float current_trim,
                      float steer_to_yaw_rate_dps);
  void StopTrimCalib() { trim_calib_.Stop(); }
  [[nodiscard]] bool IsTrimCalibActive() const {
    return trim_calib_.IsActive();
  }
  [[nodiscard]] SteeringTrimCalibration::Result GetTrimCalibResult() const {
    return trim_calib_.GetResult();
  }

  // ── CoM Offset Calibration ──────────────────────────────────────────
  bool StartComCalib(float target_accel_g, float steering_magnitude,
                     float cruise_duration_sec, const float* gravity_vec);
  void StopComCalib() { com_calib_.Stop(); }
  [[nodiscard]] bool IsComCalibActive() const { return com_calib_.IsActive(); }
  [[nodiscard]] ComOffsetCalibration::Result GetComCalibResult() const {
    return com_calib_.GetResult();
  }

  // ── Test Runner ─────────────────────────────────────────────────────
  bool StartTest(const TestParams& params);
  void StopTest() { test_runner_.Stop(); }
  [[nodiscard]] bool IsTestActive() const { return test_runner_.IsActive(); }
  [[nodiscard]] TestRunner::Status GetTestStatus() const {
    return test_runner_.GetStatus();
  }
  [[nodiscard]] float GetTestMarker() const {
    return test_runner_.GetTestMarker();
  }

  // ── Speed Calibration ────────────────────────────────────────────────
  bool StartSpeedCalib(float target_throttle = 0.3f,
                       float cruise_duration_sec = 3.0f);
  void StopSpeedCalib() { speed_calib_.Stop(); }
  [[nodiscard]] bool IsSpeedCalibActive() const {
    return speed_calib_.IsActive();
  }
  [[nodiscard]] SpeedCalibration::Result GetSpeedCalibResult() const {
    return speed_calib_.GetResult();
  }

  /** Остановить все процедуры (вызывается из failsafe). */
  void StopAll();

 private:
  CalibrationManager* calib_mgr_{nullptr};
  SteeringTrimCalibration trim_calib_;
  ComOffsetCalibration com_calib_;
  TestRunner test_runner_;
  SpeedCalibration speed_calib_;
};

}  // namespace rc_vehicle
