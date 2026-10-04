#include <gtest/gtest.h>

#include "rc_pulse_decoder.hpp"

using namespace rc_vehicle;

namespace {

// Один импульс: RISE в rise_us, FALL через width_us. Возвращает момент FALL.
uint64_t Pulse(RcPulseDecoder& dec, uint64_t rise_us, uint32_t width_us) {
  dec.OnEdge(true, rise_us);
  dec.OnEdge(false, rise_us + width_us);
  return rise_us + width_us;
}

class RcPulseDecoderTest : public ::testing::Test {
 protected:
  RcPulseConfig cfg_{};  // 1000/1500/2000, допуск 200, таймаут 250 мс
  RcPulseDecoder dec_{cfg_};
};

TEST_F(RcPulseDecoderTest, InactiveBeforeFirstPulse) {
  EXPECT_FALSE(dec_.IsActive(1'000'000));
  EXPECT_FALSE(dec_.Read(1'000'000).has_value());
}

TEST_F(RcPulseDecoderTest, NeutralAndExtremesNormalize) {
  Pulse(dec_, 1000, 1500);
  EXPECT_FLOAT_EQ(*dec_.Read(3000), 0.0f);
  Pulse(dec_, 30'000, 1000);
  EXPECT_FLOAT_EQ(*dec_.Read(32'000), -1.0f);
  Pulse(dec_, 50'000, 2000);
  EXPECT_FLOAT_EQ(*dec_.Read(53'000), 1.0f);
}

TEST_F(RcPulseDecoderTest, HalfDeflectionIsSymmetric) {
  Pulse(dec_, 1000, 1250);
  EXPECT_FLOAT_EQ(*dec_.Read(3000), -0.5f);
  Pulse(dec_, 30'000, 1750);
  EXPECT_FLOAT_EQ(*dec_.Read(32'000), 0.5f);
}

// LOS-216: раньше окно было строго [1000, 2000] и такие импульсы выбрасывались.
TEST_F(RcPulseDecoderTest, SlightlyOutOfRangePulsesAreAcceptedAndClamped) {
  Pulse(dec_, 1000, 990);
  EXPECT_FLOAT_EQ(*dec_.Read(3000), -1.0f);
  Pulse(dec_, 30'000, 2010);
  EXPECT_FLOAT_EQ(*dec_.Read(33'000), 1.0f);

  EXPECT_EQ(dec_.Stats().accepted, 2u);
  EXPECT_EQ(dec_.Stats().rejected_short, 0u);
  EXPECT_EQ(dec_.Stats().rejected_long, 0u);
}

TEST_F(RcPulseDecoderTest, ToleranceBoundariesAreInclusive) {
  Pulse(dec_, 1000, 800);
  Pulse(dec_, 30'000, 2200);
  EXPECT_EQ(dec_.Stats().accepted, 2u);
}

TEST_F(RcPulseDecoderTest, PulsesBeyondToleranceAreRejectedAndCounted) {
  const uint64_t t = Pulse(dec_, 1000, 1500);

  Pulse(dec_, 30'000, 799);
  Pulse(dec_, 60'000, 2201);

  EXPECT_EQ(dec_.Stats().rejected_short, 1u);
  EXPECT_EQ(dec_.Stats().rejected_long, 1u);
  EXPECT_EQ(dec_.Stats().last_rejected_width_us, 2201u);
  EXPECT_EQ(dec_.Stats().accepted, 1u);
  EXPECT_EQ(dec_.Stats().last_width_us, 1500u);
  // Отбракованный импульс не обновляет значение и не продлевает активность.
  EXPECT_FLOAT_EQ(*dec_.Read(t + 1000), 0.0f);
  EXPECT_FALSE(dec_.IsActive(t + 250'000));
}

TEST_F(RcPulseDecoderTest, FallWithoutRiseIsIgnored) {
  dec_.OnEdge(false, 5000);
  EXPECT_FALSE(dec_.IsActive(5000));
  EXPECT_EQ(dec_.Stats().accepted, 0u);
  EXPECT_EQ(dec_.Stats().rejected_short, 0u);
}

TEST_F(RcPulseDecoderTest, RepeatedRiseUsesLatestEdge) {
  dec_.OnEdge(true, 1000);
  dec_.OnEdge(true, 5000);  // FALL пропущен — берём последний RISE
  dec_.OnEdge(false, 6500);
  EXPECT_EQ(dec_.Stats().last_width_us, 1500u);
}

TEST_F(RcPulseDecoderTest, TimeoutDropsSignalAndNewPulseRecoversIt) {
  const uint64_t t = Pulse(dec_, 1000, 1500);

  EXPECT_TRUE(dec_.IsActive(t + 249'999));
  EXPECT_FALSE(dec_.IsActive(t + 250'000));
  EXPECT_FALSE(dec_.Read(t + 250'000).has_value());

  const uint64_t t2 = Pulse(dec_, t + 300'000, 1600);
  EXPECT_TRUE(dec_.IsActive(t2));
  EXPECT_NEAR(*dec_.Read(t2), 0.2f, 1e-6f);
}

// Сценарий LOS-216: ручка у упора, 2010 мкс каждые 20 мс, 1 секунда.
// Со старым окном [1000, 2000] канал уходил в таймаут уже через 250 мс.
TEST_F(RcPulseDecoderTest, StickHeldAtEndStopStaysActive) {
  uint64_t t = 0;
  for (int i = 0; i < 50; ++i) {
    t = Pulse(dec_, 20'000ull * (i + 1), 2010);
    ASSERT_TRUE(dec_.IsActive(t)) << "потерян на импульсе " << i;
    EXPECT_FLOAT_EQ(*dec_.Read(t), 1.0f);
  }
  EXPECT_EQ(dec_.Stats().rejected_long, 0u);
}

// Раньше «нет импульса» кодировалось нулевым временем, и первый RISE в t=0
// был неотличим от «не было фронта».
TEST_F(RcPulseDecoderTest, FirstEdgeAtTimeZeroIsAccepted) {
  Pulse(dec_, 0, 1500);
  EXPECT_TRUE(dec_.IsActive(1500));
  EXPECT_EQ(dec_.Stats().accepted, 1u);
}

// uint32_t-время переполнялось через ~71 мин; 64-бит этого не знает.
TEST_F(RcPulseDecoderTest, PulseAcrossUint32BoundaryIsMeasuredCorrectly) {
  const uint64_t rise = 0xFFFF'FF00ull;
  Pulse(dec_, rise, 1500);
  EXPECT_EQ(dec_.Stats().last_width_us, 1500u);
  EXPECT_TRUE(dec_.IsActive(rise + 1500 + 100'000));
}

TEST_F(RcPulseDecoderTest, CopyKeepsStateIndependently) {
  Pulse(dec_, 1000, 1500);
  const RcPulseDecoder snapshot = dec_;

  Pulse(dec_, 30'000, 2000);

  EXPECT_FLOAT_EQ(*snapshot.Read(3000), 0.0f);
  EXPECT_EQ(snapshot.Stats().accepted, 1u);
  EXPECT_EQ(dec_.Stats().accepted, 2u);
}

}  // namespace
