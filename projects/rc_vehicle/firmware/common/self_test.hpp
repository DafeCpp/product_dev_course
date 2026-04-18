#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace rc_vehicle {

/**
 * @brief Результат одной проверки self-test
 */
struct SelfTestItem {
  const char* name{""};
  bool passed{false};
  char value[48]{};  ///< Human-readable значение (например "498 Hz")

  SelfTestItem() = default;
  SelfTestItem(const char* n, bool p, const char* v = "")
      : name(n), passed(p) {
    std::strncpy(value, v, sizeof(value) - 1);
    value[sizeof(value) - 1] = '\0';
  }
};

/**
 * @brief Входные данные для self-test (snapshot текущего состояния)
 *
 * Заполняется вызывающим кодом из компонентов control loop.
 * Разделение на структуру позволяет тестировать SelfTest через GTest
 * без реального железа.
 */
struct SelfTestInput {
  // Control loop
  uint32_t loop_hz{0};

  // IMU
  bool imu_enabled{false};
  float gyro_x_dps{0};
  float gyro_y_dps{0};
  float gyro_z_dps{0};
  float accel_x_g{0};
  float accel_y_g{0};
  float accel_z_g{0};

  // Madgwick orientation
  float pitch_deg{0};
  float roll_deg{0};

  // EKF
  float ekf_vx{0};
  float ekf_vy{0};

  // Failsafe
  bool failsafe_active{false};

  // Calibration
  bool calib_valid{false};

  // TelemetryLog
  size_t log_capacity{0};

  // PWM (0 = ok, nonzero = error)
  int pwm_status{0};
};

/**
 * @brief Автоматический self-test прошивки
 *
 * Проверяет основные подсистемы по snapshot текущего состояния.
 * Не блокирует control loop — работает с мгновенными значениями.
 *
 * Предназначен для запуска в квартире при отладке на столе:
 * машина неподвижна, IMU откалиброван, WiFi подключён.
 */
class SelfTest {
 public:
  /**
   * @brief Выполнить все проверки
   * @param input Snapshot текущего состояния подсистем
   * @return Вектор результатов (10 проверок)
   */
  static std::vector<SelfTestItem> Run(const SelfTestInput& input);

  /**
   * @brief Проверить, все ли тесты прошли
   * @param results Результаты Run()
   * @return true если все passed
   */
  static bool AllPassed(const std::vector<SelfTestItem>& results);
};

}  // namespace rc_vehicle
