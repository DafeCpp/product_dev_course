#pragma once

#include <cstdint>

namespace rc_vehicle {

/** Magic number для проверки валидности NVS-записи ('STB2') */
static constexpr uint32_t kStabilizationConfigMagic = 0x53544232;

/**
 * @brief Режим стабилизации
 * 0 = Normal  — базовый контроль рыскания
 * 1 = Sport   — агрессивные параметры, высокая отзывчивость
 * 2 = Drift   — мягкий контроль, управление заносом
 */
enum class DriveMode : uint8_t { Normal = 0, Sport = 1, Drift = 2 };

/**
 * @brief Конфигурация ПИД-регулятора
 */
struct PidConfig {
  /** Пропорциональный коэффициент */
  float kp{0.1f};

  /** Интегральный коэффициент */
  float ki{0.0f};

  /** Дифференциальный коэффициент */
  float kd{0.005f};

  /**
   * Anti-windup: ограничение накопителя интегратора.
   * Единицы зависят от контекста (deg/s для yaw, градусы·с для slip).
   */
  float max_integral{0.5f};

  /**
   * Максимальная поправка от ПИД [-1..1] или [0..1].
   * Ограничивает влияние регулятора на команду.
   */
  float max_correction{0.3f};

  /**
   * @brief Проверить валидность конфигурации ПИД
   */
  [[nodiscard]] bool IsValid() const noexcept {
    return kp >= 0.0f && ki >= 0.0f && kd >= 0.0f && max_integral >= 0.0f &&
           max_correction > 0.0f;
  }

  /**
   * @brief Применить ограничения к параметрам
   */
  void Clamp() noexcept;
};

/**
 * @brief Конфигурация фильтров (Madgwick AHRS и LPF Butterworth)
 */
struct FilterConfig {
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
   * @brief Проверить валидность конфигурации фильтров
   */
  [[nodiscard]] bool IsValid() const noexcept {
    return madgwick_beta > 0.0f && madgwick_beta <= 1.0f &&
           lpf_cutoff_hz >= 5.0f && lpf_cutoff_hz <= 100.0f &&
           imu_sample_rate_hz > 0.0f;
  }

  /**
   * @brief Применить ограничения к параметрам
   */
  void Clamp() noexcept;
};

/**
 * @brief Конфигурация адаптивного масштабирования ПИД по скорости
 */
struct AdaptiveConfig {
  /**
   * Включить адаптивное масштабирование yaw PID по скорости из EKF.
   * При включении выход ПИД умножается на (speed / speed_ref), зажатый в
   * [scale_min..scale_max].
   */
  bool enabled{false};

  /**
   * Эталонная скорость для адаптивного ПИД [м/с].
   * При speed == speed_ref масштаб равен 1.0.
   * Диапазон: 0.1–10 m/s.
   */
  float speed_ref_ms{1.5f};

  /**
   * Минимальный коэффициент масштабирования ПИД (при низкой скорости).
   * Диапазон: 0.1–1.0.
   */
  float scale_min{0.5f};

  /**
   * Максимальный коэффициент масштабирования ПИД (при высокой скорости).
   * Диапазон: 1.0–5.0.
   */
  float scale_max{2.0f};

  /**
   * @brief Проверить валидность конфигурации адаптивного ПИД
   */
  [[nodiscard]] bool IsValid() const noexcept {
    return speed_ref_ms >= 0.1f && speed_ref_ms <= 10.0f && scale_min >= 0.1f &&
           scale_min <= 1.0f && scale_max >= 1.0f && scale_max <= 5.0f;
  }

  /**
   * @brief Применить ограничения к параметрам
   */
  void Clamp() noexcept;
};

/**
 * @brief Конфигурация предупреждения о заносе (oversteer prediction)
 */
struct OversteerConfig {
  /**
   * Включить предупреждение о заносе (oversteer prediction).
   * При превышении порогов slip angle и скорости его изменения
   * устанавливается флаг oversteer и опционально снижается газ.
   */
  bool warn_enabled{false};

  /**
   * Порог модуля угла заноса для срабатывания oversteer [градусы].
   * Диапазон: 5–45 deg.
   */
  float slip_thresh_deg{20.0f};

  /**
   * Порог скорости изменения угла заноса для срабатывания [deg/s].
   * Диапазон: 10–500 dps.
   */
  float rate_thresh_deg_s{50.0f};

  /**
   * Снижение газа при срабатывании oversteer: throttle *= (1 - reduction).
   * 0 = не снижать, 1 = полный стоп.
   * Диапазон: 0–1. Активно только в режимах Normal/Sport (не Drift).
   */
  float throttle_reduction{0.0f};

  /**
   * @brief Проверить валидность конфигурации oversteer
   */
  [[nodiscard]] bool IsValid() const noexcept {
    return slip_thresh_deg >= 5.0f && slip_thresh_deg <= 45.0f &&
           rate_thresh_deg_s >= 10.0f && rate_thresh_deg_s <= 500.0f &&
           throttle_reduction >= 0.0f && throttle_reduction <= 1.0f;
  }

  /**
   * @brief Применить ограничения к параметрам
   */
  void Clamp() noexcept;
};

/**
 * @brief Конфигурация yaw rate контроллера
 */
struct YawRateConfig {
  /** ПИД-регулятор yaw rate */
  PidConfig pid;

  /**
   * Масштаб: steer_command [-1..1] → желаемая угловая скорость (deg/s).
   * При steer=1.0 желаемая угловая скорость = steer_to_yaw_rate_dps.
   * Диапазон: 10–360 deg/s.
   */
  float steer_to_yaw_rate_dps{90.0f};

  /**
   * @brief Проверить валидность конфигурации yaw rate
   */
  [[nodiscard]] bool IsValid() const noexcept {
    return pid.IsValid() && steer_to_yaw_rate_dps > 0.0f;
  }

  /**
   * @brief Применить ограничения к параметрам
   */
  void Clamp() noexcept;
};

/**
 * @brief Конфигурация slip angle контроллера (для режима Drift)
 */
struct SlipAngleConfig {
  /** ПИД-регулятор slip angle → throttle */
  PidConfig pid;

  /**
   * Целевой угол заноса (градусы).
   * Диапазон: -45..45, по умолчанию 0 (нет заноса).
   * Активен только в Drift mode.
   */
  float target_deg{0.0f};

  /**
   * @brief Проверить валидность конфигурации slip angle
   */
  [[nodiscard]] bool IsValid() const noexcept {
    return pid.IsValid() && target_deg >= -45.0f && target_deg <= 45.0f;
  }

  /**
   * @brief Применить ограничения к параметрам
   */
  void Clamp() noexcept;
};

/**
 * @brief Конфигурация pitch compensation (стабилизация на склонах)
 */
struct PitchCompensationConfig {
  /**
   * Включена ли компенсация наклона (pitch).
   * При включении корректирует газ в зависимости от угла наклона.
   */
  bool enabled{false};

  /**
   * Коэффициент pitch → throttle (дельта газа на градус наклона).
   * Положительное значение: наклон носом вверх (подъём) → увеличить газ.
   * Диапазон: 0.0–0.05, по умолчанию 0.01 (1% газа на градус).
   */
  float gain{0.01f};

  /**
   * Максимальная поправка газа от компенсации наклона [0..1].
   * Ограничивает влияние алгоритма на команду газа.
   * Диапазон: 0.0–0.5, по умолчанию 0.25.
   */
  float max_correction{0.25f};

  /**
   * @brief Проверить валидность конфигурации pitch compensation
   */
  [[nodiscard]] bool IsValid() const noexcept {
    return gain >= 0.0f && gain <= 0.05f && max_correction >= 0.0f &&
           max_correction <= 0.5f;
  }

  /**
   * @brief Применить ограничения к параметрам
   */
  void Clamp() noexcept;
};

/**
 * @brief Конфигурация системы стабилизации
 *
 * Улучшенная структура с группировкой связанных параметров в подструктуры.
 * Параметры фильтрации и режимов стабилизации, сохраняемые в NVS.
 * Используется для настройки Madgwick AHRS и LPF Баттерворта через WebSocket
 * API.
 */
struct StabilizationConfig {
  /** Включена ли стабилизация (по умолчанию выключена) */
  bool enabled{false};

  /**
   * Текущий режим стабилизации.
   * Normal (0) = базовый контроль рыскания
   * Sport (1)  = более агрессивные параметры
   * Drift (2)  = управление дрифтом
   */
  DriveMode mode{DriveMode::Normal};

  /**
   * Время нарастания/спада веса стабилизации (мс).
   * При включении/выключении стабилизации поправка руля плавно
   * нарастает/убывает за это время. 0 = мгновенное переключение.
   * Диапазон: 0–5000 мс, по умолчанию 500 мс.
   */
  uint32_t fade_ms{500};

  /** Конфигурация фильтров (Madgwick, LPF) */
  FilterConfig filter;

  /** Конфигурация yaw rate контроллера */
  YawRateConfig yaw_rate;

  /** Конфигурация slip angle контроллера */
  SlipAngleConfig slip_angle;

  /** Конфигурация адаптивного масштабирования ПИД */
  AdaptiveConfig adaptive;

  /** Конфигурация предупреждения о заносе */
  OversteerConfig oversteer;

  /** Конфигурация pitch compensation */
  PitchCompensationConfig pitch_comp;

  /** Версия структуры для NVS-миграции */
  uint8_t version{2};

  /** Валидность конфигурации (magic number для проверки NVS) */
  uint32_t magic{kStabilizationConfigMagic};

  /**
   * @brief Проверить валидность конфигурации
   * @return true если конфигурация валидна
   */
  [[nodiscard]] bool IsValid() const noexcept;

  /**
   * @brief Сбросить конфигурацию к значениям по умолчанию
   */
  void Reset() noexcept;

  /**
   * @brief Применить предустановки для текущего режима (mode).
   *
   * Устанавливает параметры ПИД и других контроллеров в зависимости от mode:
   *   Normal — консервативный контроль рыскания
   *   Sport  — агрессивные параметры, высокая отзывчивость
   *   Drift  — мягкий контроль, допускает занос
   *
   * Не изменяет: enabled, filter, fade_ms, magic.
   */
  void ApplyModeDefaults() noexcept;

  /**
   * @brief Применить ограничения к параметрам
   */
  void Clamp() noexcept;
};

namespace config {
/** @brief Значения конфигурации стабилизации по умолчанию */
struct StabilizationDefaults {
  static constexpr bool kEnabled = false;
  static constexpr float kMadgwickBeta = 0.1f;
  static constexpr float kLpfCutoffHz = 30.0f;
  static constexpr float kImuSampleRateHz = 500.0f;
  static constexpr DriveMode kMode = DriveMode::Normal;
};
}  // namespace config

}  // namespace rc_vehicle
