#include <gtest/gtest.h>

#include "drive_mode_registry.hpp"
#include "drive_mode_strategy.hpp"
#include "drive_modes.hpp"
#include "stabilization_config.hpp"

using namespace rc_vehicle;

// ══════════════════════════════════════════════════════════════════════════════
// Registry constants
// ══════════════════════════════════════════════════════════════════════════════

TEST(DriveModeRegistryTest, kMaxModes_IsExactlyFive) {
  EXPECT_EQ(DriveModeRegistry::kMaxModes, 5u);
}

// ══════════════════════════════════════════════════════════════════════════════
// Boundary: last valid index resolves, first out-of-range falls back
// ══════════════════════════════════════════════════════════════════════════════

TEST(DriveModeRegistryTest, LastValidIndex_ReturnsDirectLaw) {
  // DriveMode::DirectLaw == 4 == kMaxModes - 1
  const auto& s = DriveModeRegistry::Get(DriveMode::DirectLaw);
  EXPECT_EQ(s.GetMode(), DriveMode::DirectLaw);
}

TEST(DriveModeRegistryTest, IndexEqualToKMaxModes_FallsBackToNormal) {
  // Value 5 is exactly kMaxModes — first out-of-range slot
  auto just_over = static_cast<DriveMode>(DriveModeRegistry::kMaxModes);
  EXPECT_EQ(DriveModeRegistry::Get(just_over).GetMode(), DriveMode::Normal);
}

// ══════════════════════════════════════════════════════════════════════════════
// Multiple out-of-range values all resolve to Normal
// ══════════════════════════════════════════════════════════════════════════════

TEST(DriveModeRegistryTest, OutOfRange_AlwaysFallsBackToNormal) {
  for (uint8_t v : {5u, 10u, 100u, 127u, 255u}) {
    auto mode = static_cast<DriveMode>(v);
    EXPECT_EQ(DriveModeRegistry::Get(mode).GetMode(), DriveMode::Normal)
        << "value=" << static_cast<unsigned>(v);
  }
}

// ══════════════════════════════════════════════════════════════════════════════
// kStrategies ordering invariant: index == static_cast<size_t>(mode)
// ══════════════════════════════════════════════════════════════════════════════

TEST(DriveModeRegistryTest, StrategyAtIndexMatchesModeOrdinal) {
  constexpr std::pair<DriveMode, uint8_t> kExpected[] = {
      {DriveMode::Normal, 0},
      {DriveMode::Sport, 1},
      {DriveMode::Drift, 2},
      {DriveMode::Kids, 3},
      {DriveMode::DirectLaw, 4},
  };
  for (auto [mode, ordinal] : kExpected) {
    EXPECT_EQ(static_cast<uint8_t>(DriveModeRegistry::Get(mode).GetMode()),
              ordinal)
        << "ordinal=" << static_cast<unsigned>(ordinal);
  }
}

// ══════════════════════════════════════════════════════════════════════════════
// Exact name strings per mode
// ══════════════════════════════════════════════════════════════════════════════

TEST(DriveModeRegistryTest, ExactNamesForAllModes) {
  EXPECT_STREQ(DriveModeRegistry::Get(DriveMode::Normal).GetName(), "Normal");
  EXPECT_STREQ(DriveModeRegistry::Get(DriveMode::Sport).GetName(), "Sport");
  EXPECT_STREQ(DriveModeRegistry::Get(DriveMode::Drift).GetName(), "Drift");
  EXPECT_STREQ(DriveModeRegistry::Get(DriveMode::Kids).GetName(), "Kids");
  EXPECT_STREQ(DriveModeRegistry::Get(DriveMode::DirectLaw).GetName(),
               "DirectLaw");
}

TEST(DriveModeRegistryTest, FallbackName_IsNormal) {
  auto invalid = static_cast<DriveMode>(200);
  EXPECT_STREQ(DriveModeRegistry::Get(invalid).GetName(), "Normal");
}

// ══════════════════════════════════════════════════════════════════════════════
// Referential stability: repeated Get() for same mode returns same object
// ══════════════════════════════════════════════════════════════════════════════

TEST(DriveModeRegistryTest, RepeatedGet_ReturnsSameReference) {
  const IDriveModeStrategy* a = &DriveModeRegistry::Get(DriveMode::Drift);
  const IDriveModeStrategy* b = &DriveModeRegistry::Get(DriveMode::Drift);
  EXPECT_EQ(a, b);
}

TEST(DriveModeRegistryTest, FallbackReference_IsSameAckrossMultipleCalls) {
  auto invalid = static_cast<DriveMode>(99);
  const IDriveModeStrategy* a = &DriveModeRegistry::Get(invalid);
  const IDriveModeStrategy* b = &DriveModeRegistry::Get(invalid);
  EXPECT_EQ(a, b);
}

// ══════════════════════════════════════════════════════════════════════════════
// Get() is distinct per mode — strategies are not aliased to each other
// ══════════════════════════════════════════════════════════════════════════════

TEST(DriveModeRegistryTest, AllModesHaveDistinctStrategyObjects) {
  const IDriveModeStrategy* ptrs[DriveModeRegistry::kMaxModes] = {
      &DriveModeRegistry::Get(DriveMode::Normal),
      &DriveModeRegistry::Get(DriveMode::Sport),
      &DriveModeRegistry::Get(DriveMode::Drift),
      &DriveModeRegistry::Get(DriveMode::Kids),
      &DriveModeRegistry::Get(DriveMode::DirectLaw),
  };
  for (size_t i = 0; i < DriveModeRegistry::kMaxModes; ++i) {
    for (size_t j = i + 1; j < DriveModeRegistry::kMaxModes; ++j) {
      EXPECT_NE(ptrs[i], ptrs[j]) << "i=" << i << " j=" << j;
    }
  }
}
