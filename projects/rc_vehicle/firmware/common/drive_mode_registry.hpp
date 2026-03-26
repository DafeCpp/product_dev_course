#pragma once

#include <array>
#include <cstddef>

#include "drive_mode_strategy.hpp"
#include "drive_modes.hpp"

namespace rc_vehicle {

/**
 * @brief Реестр стратегий режимов вождения.
 *
 * Статический массив стратегий — без аллокаций, O(1) поиск по DriveMode.
 *
 * Добавление нового режима:
 * 1) Создать класс-стратегию в drive_modes.hpp/.cpp
 * 2) Добавить значение в enum DriveMode
 * 3) Добавить экземпляр в kStrategies
 */
class DriveModeRegistry {
 public:
  static constexpr size_t kMaxModes = 5;

  /**
   * @brief Получить стратегию по DriveMode
   * @return Ссылка на стратегию (fallback на Normal при невалидном mode)
   */
  static const IDriveModeStrategy& Get(DriveMode mode) noexcept {
    const auto idx = static_cast<size_t>(mode);
    if (idx < kMaxModes) {
      return *kStrategies[idx];
    }
    return kNormal;
  }

 private:
  static const NormalModeStrategy kNormal;
  static const SportModeStrategy kSport;
  static const DriftModeStrategy kDrift;
  static const KidsModeStrategy kKids;
  static const DirectLawStrategy kDirectLaw;

  static const std::array<const IDriveModeStrategy*, kMaxModes> kStrategies;
};

}  // namespace rc_vehicle
