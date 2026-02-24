#pragma once

#include <cstdint>

/**
 * @brief Конфигурация системы стабилизации
 *
 * Параметры фильтрации и режимов стабилизации, сохраняемые в NVS.
 * Используется для настройки Madgwick AHRS и LPF Butterworth через WebSocket
 * API.
 */
struct StabilizationConfig {
  /** Включена ли стабилизация (по умолчанию выключена) */
  bool enabled{false};

  /**
   * Коэффициент коррекции Madgwick (beta).
   * Диапазон: 0.01–1.0, по умолчанию 0.1.
   * Больше значение → быстрее реакция на акселерометр, но больше шум.
   * Меньше значение → медленнее реакция, но стабильнее ориентация.
   */
  float madgwick_beta{0.1f};

  /**
   * Частота среза LPF Butterworth для gyro Z (Hz).
   * Диапазон: 5–100 Hz, по умолчанию 30 Hz.
   * Используется для фильтрации угловой скорости перед ПИД контролем рыскания.
   * Меньше → сильнее фильтрация (меньше шум, но больше задержка).
   * Больше → слабее фильтрация (быстрее отклик, но больше шум).
   */
  float lpf_cutoff_hz{30.0f};

  /**
   * Частота дискретизации IMU (Hz).
   * По умолчанию 500 Hz (период 2 мс).
   * Используется для настройки LPF Butterworth.
   */
  float imu_sample_rate_hz{500.0f};

  /**
   * Режим стабилизации (для будущего расширения).
   * 0 = normal (базовый контроль рыскания)
   * 1 = sport (более агрессивные параметры)
   * 2 = drift (управление дрифтом)
   */
  uint8_t mode{0};

  // ── ПИД-регулятор yaw rate ──────────────────────────────────────────────

  /** Пропорциональный коэффициент ПИД */
  float pid_kp{0.1f};

  /** Интегральный коэффициент ПИД */
  float pid_ki{0.0f};

  /** Дифференциальный коэффициент ПИД */
  float pid_kd{0.005f};

  /**
   * Anti-windup: ограничение накопителя интегратора.
   * Единицы: deg/s (т.к. ошибка в deg/s).
   */
  float pid_max_integral{0.5f};

  /**
   * Максимальная поправка руля от ПИД [-1..1].
   * Ограничивает влияние регулятора на команду руления.
   */
  float pid_max_correction{0.3f};

  /**
   * Масштаб: steer_command [-1..1] → желаемая угловая скорость (deg/s).
   * При steer=1.0 желаемая угловая скорость = steer_to_yaw_rate_dps.
   * Диапазон: 10–360 deg/s.
   */
  float steer_to_yaw_rate_dps{90.0f};

  /**
   * Время нарастания/спада веса стабилизации (мс).
   * При включении/выключении стабилизации поправка руля плавно
   * нарастает/убывает за это время. 0 = мгновенное переключение.
   * Диапазон: 0–5000 мс, по умолчанию 500 мс.
   */
  uint32_t fade_ms{500};

  /** Валидность конфигурации (magic number для проверки NVS) */
  uint32_t magic{0x53544142};  // 'STAB'

  /**
   * @brief Проверить валидность конфигурации
   * @return true если конфигурация валидна
   */
  [[nodiscard]] bool IsValid() const noexcept {
    return magic == 0x53544142 && madgwick_beta > 0.0f &&
           madgwick_beta <= 1.0f && lpf_cutoff_hz >= 5.0f &&
           lpf_cutoff_hz <= 100.0f && imu_sample_rate_hz > 0.0f &&
           pid_kp >= 0.0f && pid_ki >= 0.0f && pid_kd >= 0.0f &&
           pid_max_correction > 0.0f && steer_to_yaw_rate_dps > 0.0f;
  }

  /**
   * @brief Сбросить конфигурацию к значениям по умолчанию
   */
  void Reset() noexcept {
    enabled = false;
    madgwick_beta = 0.1f;
    lpf_cutoff_hz = 30.0f;
    imu_sample_rate_hz = 500.0f;
    mode = 0;
    pid_kp = 0.1f;
    pid_ki = 0.0f;
    pid_kd = 0.005f;
    pid_max_integral = 0.5f;
    pid_max_correction = 0.3f;
    steer_to_yaw_rate_dps = 90.0f;
    fade_ms = 500;
    magic = 0x53544142;
  }

  /**
   * @brief Применить ограничения к параметрам
   */
  void Clamp() noexcept {
    if (madgwick_beta < 0.01f) madgwick_beta = 0.01f;
    if (madgwick_beta > 1.0f) madgwick_beta = 1.0f;
    if (lpf_cutoff_hz < 5.0f) lpf_cutoff_hz = 5.0f;
    if (lpf_cutoff_hz > 100.0f) lpf_cutoff_hz = 100.0f;
    if (imu_sample_rate_hz < 100.0f) imu_sample_rate_hz = 100.0f;
    if (mode > 2) mode = 0;
    if (pid_kp < 0.0f) pid_kp = 0.0f;
    if (pid_ki < 0.0f) pid_ki = 0.0f;
    if (pid_kd < 0.0f) pid_kd = 0.0f;
    if (pid_max_integral < 0.0f) pid_max_integral = 0.0f;
    if (pid_max_correction < 0.0f) pid_max_correction = 0.0f;
    if (pid_max_correction > 1.0f) pid_max_correction = 1.0f;
    if (steer_to_yaw_rate_dps < 10.0f) steer_to_yaw_rate_dps = 10.0f;
    if (steer_to_yaw_rate_dps > 360.0f) steer_to_yaw_rate_dps = 360.0f;
    if (fade_ms > 5000) fade_ms = 5000;
  }
};

/**
 * @brief Конфигурация стабилизации по умолчанию
 */
namespace rc_vehicle::config {
struct StabilizationDefaults {
  static constexpr bool kEnabled = false;
  static constexpr float kMadgwickBeta = 0.1f;
  static constexpr float kLpfCutoffHz = 30.0f;
  static constexpr float kImuSampleRateHz = 500.0f;
  static constexpr uint8_t kMode = 0;
};
}  // namespace rc_vehicle::config