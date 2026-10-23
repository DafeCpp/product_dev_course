#include "motor_config.hpp"

#include <cmath>

#include "rc_vehicle_common.hpp"

namespace rc_vehicle {

bool MotorConfig::IsValid() const noexcept {
  if (static_cast<uint8_t>(profile) >= kMotorProfileCount) return false;
  if (min_us < kMotorPulseMinUs || max_us > kMotorPulseMaxUs) return false;
  if (neutral_us < min_us + kMotorMinTravelUs) return false;
  if (max_us < neutral_us + kMotorMinTravelUs) return false;
  if (!std::isfinite(deadband) || deadband < 0.0f || deadband > kMaxDeadband)
    return false;
  return true;
}

MotorConfig MotorConfig::Defaults(MotorProfile profile) noexcept {
  MotorConfig cfg{};
  switch (profile) {
    case MotorProfile::VescPwm:
      // Диапазон PPM задаётся в VESC Tool под этот же 1000–2000 мкс; своя
      // мёртвая зона у VESC тоже есть, поэтому здесь по умолчанию 0.
      cfg.profile = MotorProfile::VescPwm;
      break;
    case MotorProfile::BrushedPwm:
    default:
      cfg.profile = MotorProfile::BrushedPwm;
      break;
  }
  return cfg;
}

uint16_t ThrottlePulseUs(const MotorConfig& cfg_in, float throttle) noexcept {
  const MotorConfig cfg = cfg_in.IsValid()
                              ? cfg_in
                              : MotorConfig::Defaults(MotorProfile::BrushedPwm);

  if (!std::isfinite(throttle)) return cfg.neutral_us;
  float value = ClampNormalized(throttle);
  if (cfg.reversed) value = -value;

  const float magnitude = std::fabs(value);
  if (magnitude <= cfg.deadband) return cfg.neutral_us;
  if (cfg.deadband > 0.0f) {
    // Растягиваем (deadband..1] → (0..1], чтобы на краю зоны не было скачка.
    const float scaled = (magnitude - cfg.deadband) / (1.0f - cfg.deadband);
    value = std::copysign(scaled, value);
  }

  return PulseWidthUsFromNormalized(value, cfg.min_us, cfg.neutral_us,
                                    cfg.max_us);
}

const char* MotorProfileToString(MotorProfile profile) noexcept {
  switch (profile) {
    case MotorProfile::BrushedPwm:
      return "brushed_pwm";
    case MotorProfile::VescPwm:
      return "vesc_pwm";
  }
  return "unknown";
}

std::optional<MotorProfile> MotorProfileFromString(
    std::string_view name) noexcept {
  if (name == "brushed_pwm") return MotorProfile::BrushedPwm;
  if (name == "vesc_pwm") return MotorProfile::VescPwm;
  return std::nullopt;
}

}  // namespace rc_vehicle
