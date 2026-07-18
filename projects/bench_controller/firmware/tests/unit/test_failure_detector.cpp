#include <gtest/gtest.h>

#include "specimen_failure_detector.hpp"

namespace bench {
namespace {

SpecimenFailureDetector MakeDetector() {
  return SpecimenFailureDetector({.drop_fraction = 0.3f,
                                  .window_ticks = 10,
                                  .min_force_n = 1000.0f,
                                  .arm_ticks = 25});
}

// Взвести детектор: подать arm_ticks тиков устойчивого слежения.
void Arm(SpecimenFailureDetector& d) {
  for (int i = 0; i < 30; ++i) {
    ASSERT_FALSE(d.Update(10'000.0f, 10'000.0f));
  }
  ASSERT_TRUE(d.Armed());
}

TEST(SpecimenFailureDetector, StartupLagDoesNotTrigger) {
  // Выход на режим: сила долго ниже уставки — до взведения детектор
  // молчит (ложный латч на старте программы недопустим).
  auto d = MakeDetector();
  for (int i = 0; i < 1000; ++i) {
    EXPECT_FALSE(d.Update(100.0f, 10'000.0f));
  }
  EXPECT_FALSE(d.Armed());
}

TEST(SpecimenFailureDetector, NormalTrackingArmsButDoesNotTrigger) {
  auto d = MakeDetector();
  for (int i = 0; i < 1000; ++i) {
    EXPECT_FALSE(d.Update(9'800.0f, 10'000.0f));  // ошибка 2 %
  }
  EXPECT_TRUE(d.Armed());
}

TEST(SpecimenFailureDetector, ForceCollapseTriggersWithinWindow) {
  auto d = MakeDetector();
  Arm(d);
  int ticks = 0;
  while (!d.Update(500.0f, 10'000.0f)) {  // сила рухнула
    ++ticks;
    ASSERT_LT(ticks, 20);
  }
  EXPECT_LE(ticks, 10);
  EXPECT_TRUE(d.Latched());
}

TEST(SpecimenFailureDetector, LatchPersists) {
  auto d = MakeDetector();
  Arm(d);
  for (int i = 0; i < 15; ++i) (void)d.Update(0.0f, 10'000.0f);
  ASSERT_TRUE(d.Latched());
  // Даже если сила «вернулась» — латч держится до Reset.
  EXPECT_TRUE(d.Update(10'000.0f, 10'000.0f));
  d.Reset();
  EXPECT_FALSE(d.Latched());
  EXPECT_FALSE(d.Armed());  // Reset разоружает — новый тест взводится заново
}

TEST(SpecimenFailureDetector, SmallSetpointTicksAreNeutral) {
  // Проход синуса через ноль: счётчик не растёт и не сбрасывается.
  auto d = MakeDetector();
  Arm(d);
  for (int i = 0; i < 8; ++i) (void)d.Update(0.0f, 10'000.0f);  // 8 из 10
  for (int i = 0; i < 100; ++i) {
    EXPECT_FALSE(d.Update(0.0f, 100.0f));  // |sp| < min_force
  }
  // Возврат значимой уставки: добираем оставшиеся 2 тика.
  (void)d.Update(0.0f, 10'000.0f);
  EXPECT_TRUE(d.Update(0.0f, 10'000.0f));
}

TEST(SpecimenFailureDetector, RecoveryResetsCounter) {
  auto d = MakeDetector();
  Arm(d);
  for (int i = 0; i < 9; ++i) (void)d.Update(0.0f, 10'000.0f);
  (void)d.Update(9'900.0f, 10'000.0f);  // сила вернулась — сброс
  for (int i = 0; i < 9; ++i) {
    EXPECT_FALSE(d.Update(0.0f, 10'000.0f));
  }
}

TEST(SpecimenFailureDetector, NegativeSetpointHandled) {
  // Сжатие: уставка отрицательная, сила должна следовать знаку.
  auto d = MakeDetector();
  for (int i = 0; i < 30; ++i) {
    ASSERT_FALSE(d.Update(-9'900.0f, -10'000.0f));
  }
  ASSERT_TRUE(d.Armed());
  int ticks = 0;
  while (!d.Update(0.0f, -10'000.0f)) {
    ++ticks;
    ASSERT_LT(ticks, 20);
  }
  EXPECT_TRUE(d.Latched());
}

}  // namespace
}  // namespace bench
