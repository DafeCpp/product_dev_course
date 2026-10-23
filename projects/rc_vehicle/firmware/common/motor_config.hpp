#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace rc_vehicle {

/**
 * @brief Профиль силовой части (мотор + ESC), LOS-19
 *
 * Прошивка одна на все моторы: профиль хранится в NVS и выбирается при
 * загрузке, а не флагом компиляции. Сейчас оба профиля управляют ESC
 * servo-PWM 50 Гц на том же GPIO; профиль VESC нужен для следующих шагов
 * (телеметрия и управление по UART), которые включаются только для него.
 *
 * Значения хранятся в NVS — не переупорядочивать.
 */
enum class MotorProfile : uint8_t {
  BrushedPwm = 0,  ///< Штатный коллекторный мотор + brushed ESC
  VescPwm = 1,     ///< Brushless + VESC (Flipsky FSESC 4.20), вход PPM
};

/** Число известных профилей (для проверки значения из NVS/JSON). */
inline constexpr uint8_t kMotorProfileCount = 2;

/**
 * @brief Параметры выхода газа для выбранного профиля
 *
 * Руль сюда не входит: серва одна и та же при любом моторе.
 */
struct MotorConfig {
  MotorProfile profile{MotorProfile::BrushedPwm};
  uint16_t min_us{1000};      ///< Полный назад/тормоз
  uint16_t neutral_us{1500};  ///< Нейтраль (failsafe)
  uint16_t max_us{2000};      ///< Полный газ
  /// Мёртвая зона вокруг нуля в долях газа [0..kMaxDeadband]: |throttle| ниже
  /// неё даёт нейтраль, остальной диапазон растягивается без скачка.
  float deadband{0.0f};
  bool reversed{false};  ///< Инвертировать направление (мотор перевёрнут)

  [[nodiscard]] bool IsValid() const noexcept;

  [[nodiscard]] static MotorConfig Defaults(MotorProfile profile) noexcept;

  bool operator==(const MotorConfig&) const = default;
};

/// Границы допустимых значений (проверяются в IsValid).
inline constexpr uint16_t kMotorPulseMinUs = 500;
inline constexpr uint16_t kMotorPulseMaxUs = 2500;
/// Минимальный ход от нейтрали в каждую сторону: защищает от деления
/// диапазона почти до нуля опечаткой в конфиге.
inline constexpr uint16_t kMotorMinTravelUs = 100;
inline constexpr float kMaxDeadband = 0.5f;

/**
 * @brief Ширина импульса газа для нормализованной команды
 * @param cfg Валидная конфигурация (IsValid); иначе используется Defaults
 *        профиля BrushedPwm
 * @param throttle Газ [-1..1]; NaN трактуется как нейтраль
 */
[[nodiscard]] uint16_t ThrottlePulseUs(const MotorConfig& cfg,
                                       float throttle) noexcept;

[[nodiscard]] const char* MotorProfileToString(MotorProfile profile) noexcept;

[[nodiscard]] std::optional<MotorProfile> MotorProfileFromString(
    std::string_view name) noexcept;

}  // namespace rc_vehicle
