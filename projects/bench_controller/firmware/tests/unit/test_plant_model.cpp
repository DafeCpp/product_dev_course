#include <gtest/gtest.h>

#include <cmath>

#include "hydraulic_plant_model.hpp"

namespace bench {
namespace {

constexpr float kDt = 0.002f;

HydraulicPlantModel MakePlant() {
  return HydraulicPlantModel({.spool_tau_s = 0.008f,
                              .piston_speed_mm_s = 400.0f,
                              .stiffness_n_mm = 5000.0f,
                              .position_limit_mm = 80.0f});
}

TEST(HydraulicPlantModel, SpoolFollowsFirstOrderLag) {
  auto plant = MakePlant();
  // Через τ золотник должен пройти ~63 % ступени.
  const int ticks_tau = 4;  // 8 мс / 2 мс
  HydraulicPlantModel::State s{};
  for (int i = 0; i < ticks_tau; ++i) s = plant.Step(1.0f, kDt);
  EXPECT_NEAR(s.spool, 0.63f, 0.08f);
  // Через 5τ — практически установился.
  for (int i = 0; i < ticks_tau * 4; ++i) s = plant.Step(1.0f, kDt);
  EXPECT_GT(s.spool, 0.98f);
}

TEST(HydraulicPlantModel, ForceIsStiffnessTimesPosition) {
  auto plant = MakePlant();
  HydraulicPlantModel::State s{};
  for (int i = 0; i < 100; ++i) s = plant.Step(0.5f, kDt);
  EXPECT_NEAR(s.force_n, 5000.0f * s.position_mm, 1e-2f);
  EXPECT_GT(s.position_mm, 0.0f);
}

TEST(HydraulicPlantModel, PositionClampedAtLimit) {
  auto plant = MakePlant();
  HydraulicPlantModel::State s{};
  for (int i = 0; i < 500; ++i) s = plant.Step(1.0f, kDt);  // 1 с на полном
  EXPECT_FLOAT_EQ(s.position_mm, 80.0f);
}

TEST(HydraulicPlantModel, FailureCollapsesForceNotPosition) {
  auto plant = MakePlant();
  for (int i = 0; i < 100; ++i) (void)plant.Step(0.5f, kDt);
  const float force_before = plant.GetState().force_n;
  const float pos_before = plant.GetState().position_mm;
  ASSERT_GT(force_before, 1000.0f);

  plant.TriggerFailure(0.02f);
  const auto s = plant.Step(0.0f, kDt);
  EXPECT_LT(s.force_n, force_before * 0.05f);
  EXPECT_NEAR(s.position_mm, pos_before, 1.0f);
  EXPECT_TRUE(plant.Failed());
}

TEST(HydraulicPlantModel, NegativeCommandRetractsAndCompresses) {
  auto plant = MakePlant();
  HydraulicPlantModel::State s{};
  for (int i = 0; i < 100; ++i) s = plant.Step(-0.5f, kDt);
  EXPECT_LT(s.position_mm, 0.0f);
  EXPECT_LT(s.force_n, 0.0f);
}

}  // namespace
}  // namespace bench
