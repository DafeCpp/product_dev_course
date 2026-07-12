#include <gtest/gtest.h>

#include <cmath>

#include "sine_program.hpp"

namespace bench {
namespace {

constexpr float kDt = 0.002f;  // 500 Гц

TEST(SineProgram, EmptyProgramIsFinishedAndZero) {
  SineProgram p;
  EXPECT_TRUE(p.Finished());
  EXPECT_FLOAT_EQ(p.Step(kDt), 0.0f);
}

TEST(SineProgram, TracksSineWithMeanAndAmplitude) {
  const SineProgram::Segment seg[] = {
      {.mean = 100.0f, .amplitude = 50.0f, .freq_hz = 10.0f, .cycles = 100}};
  SineProgram p{seg};

  float min_v = 1e9f, max_v = -1e9f;
  for (int i = 0; i < 500; ++i) {  // 1 с = 10 циклов
    const float v = p.Step(kDt);
    min_v = std::min(min_v, v);
    max_v = std::max(max_v, v);
  }
  EXPECT_NEAR(max_v, 150.0f, 1.0f);
  EXPECT_NEAR(min_v, 50.0f, 1.0f);
}

TEST(SineProgram, CountsCyclesAndFinishes) {
  const SineProgram::Segment seg[] = {
      {.mean = 0.0f, .amplitude = 1.0f, .freq_hz = 10.0f, .cycles = 3}};
  SineProgram p{seg};

  // 3 цикла по 100 мс при 10 Гц = 300 мс = 150 тиков (+ запас).
  for (int i = 0; i < 149; ++i) {
    (void)p.Step(kDt);
    EXPECT_FALSE(p.Finished()) << "tick " << i;
  }
  for (int i = 0; i < 5; ++i) (void)p.Step(kDt);
  EXPECT_TRUE(p.Finished());
  // После завершения — удержание среднего.
  EXPECT_FLOAT_EQ(p.Step(kDt), 0.0f);
}

TEST(SineProgram, AdvancesThroughSegments) {
  const SineProgram::Segment segs[] = {
      {.mean = 10.0f, .amplitude = 5.0f, .freq_hz = 50.0f, .cycles = 2},
      {.mean = 20.0f, .amplitude = 1.0f, .freq_hz = 10.0f, .cycles = 1}};
  SineProgram p{segs};

  // Сегмент 0: 2 цикла по 20 мс = 40 мс = 20 тиков.
  for (int i = 0; i < 21; ++i) (void)p.Step(kDt);
  EXPECT_EQ(p.SegmentIndex(), 1u);
  EXPECT_FLOAT_EQ(p.CurrentMean(), 20.0f);
  EXPECT_FALSE(p.Finished());
}

TEST(SineProgram, PhaseContinuousWithinSegment) {
  // Значение между соседними тиками не скачет больше, чем
  // амплитуда · ω · dt (максимальная производная синуса).
  const SineProgram::Segment seg[] = {
      {.mean = 0.0f, .amplitude = 100.0f, .freq_hz = 50.0f, .cycles = 100}};
  SineProgram p{seg};

  const float max_step =
      100.0f * 2.0f * 3.14159265f * 50.0f * kDt * 1.05f;  // +5 % запас
  float prev = p.Step(kDt);
  for (int i = 0; i < 1000; ++i) {
    const float v = p.Step(kDt);
    EXPECT_LE(std::fabs(v - prev), max_step) << "tick " << i;
    prev = v;
  }
}

}  // namespace
}  // namespace bench
