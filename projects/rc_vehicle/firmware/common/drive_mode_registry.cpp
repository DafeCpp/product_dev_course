#include "drive_mode_registry.hpp"

namespace rc_vehicle {

const NormalModeStrategy DriveModeRegistry::kNormal;
const SportModeStrategy DriveModeRegistry::kSport;
const DriftModeStrategy DriveModeRegistry::kDrift;
const KidsModeStrategy DriveModeRegistry::kKids;
const DirectLawStrategy DriveModeRegistry::kDirectLaw;

const std::array<const IDriveModeStrategy*, DriveModeRegistry::kMaxModes>
    DriveModeRegistry::kStrategies = {&kNormal, &kSport, &kDrift, &kKids,
                                      &kDirectLaw};

}  // namespace rc_vehicle
