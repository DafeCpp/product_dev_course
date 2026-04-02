#include <gtest/gtest.h>

#include "mag_calibration.hpp"

// ─── Helpers ──────────────────────────────────────────────────────────────────

/**
 * Feed N identical or varied samples so that min/max span is large enough.
 * Feeds samples forming a cube ±amplitude on each axis.
 */
static void FeedSphereSamples(MagCalibration& cal, int count,
                               float amplitude = 400.f) {
  // Cycle through 6 face-centre points of a cube to simulate full rotation
  const MagData faces[6] = {
      {amplitude, 0.f, 0.f},
      {-amplitude, 0.f, 0.f},
      {0.f, amplitude, 0.f},
      {0.f, -amplitude, 0.f},
      {0.f, 0.f, amplitude},
      {0.f, 0.f, -amplitude},
  };
  for (int i = 0; i < count; ++i) {
    cal.FeedSample(faces[i % 6]);
  }
}

// ─── Tests ────────────────────────────────────────────────────────────────────

TEST(MagCalibration, InitiallyIdle) {
  MagCalibration cal;
  EXPECT_EQ(cal.GetStatus(), MagCalibStatus::Idle);
  EXPECT_FALSE(cal.IsValid());
  EXPECT_FALSE(cal.IsCollecting());
}

TEST(MagCalibration, StartSetsCollecting) {
  MagCalibration cal;
  cal.Start();
  EXPECT_EQ(cal.GetStatus(), MagCalibStatus::Collecting);
  EXPECT_TRUE(cal.IsCollecting());
}

TEST(MagCalibration, FeedSamplesUpdatesMinMax) {
  MagCalibration cal;
  cal.Start();

  // Feed known min/max values so we can reason about the result
  cal.FeedSample({100.f, 200.f, 300.f});
  cal.FeedSample({-100.f, -200.f, -300.f});

  // Must still be collecting (too few samples to Finish)
  EXPECT_TRUE(cal.IsCollecting());
}

TEST(MagCalibration, FinishFailsWithTooFewSamples) {
  MagCalibration cal;
  cal.Start();

  // Feed just a few samples — below kMinSamples
  for (int i = 0; i < MagCalibration::kMinSamples - 1; ++i) {
    cal.FeedSample({400.f, 0.f, 0.f});
    cal.FeedSample({-400.f, 0.f, 0.f});
  }

  // We fed 2*(kMinSamples-1) samples, still < kMinSamples total iterations
  // Reset to make sure count is strictly < kMinSamples
  MagCalibration cal2;
  cal2.Start();
  cal2.FeedSample({400.f, 0.f, 0.f});
  cal2.Finish();
  EXPECT_EQ(cal2.GetStatus(), MagCalibStatus::Failed);
  EXPECT_FALSE(cal2.IsValid());
}

TEST(MagCalibration, FinishFailsIfRadiusTooSmall) {
  MagCalibration cal;
  cal.Start();

  // All samples nearly identical → span ≈ 0 → radius < kMinRadius
  const MagData tiny{1.f, 1.f, 1.f};
  for (int i = 0; i < MagCalibration::kMinSamples + 10; ++i) {
    cal.FeedSample(tiny);
  }

  cal.Finish();
  EXPECT_EQ(cal.GetStatus(), MagCalibStatus::Failed);
  EXPECT_FALSE(cal.IsValid());
}

TEST(MagCalibration, FinishSucceedsWithGoodData) {
  MagCalibration cal;
  cal.Start();

  // Offset the sphere centre by (+50, -30, +20) mGauss
  const float cx = 50.f, cy = -30.f, cz = 20.f;
  const float amp = 400.f;
  const MagData faces[6] = {
      {cx + amp, cy, cz},
      {cx - amp, cy, cz},
      {cx, cy + amp, cz},
      {cx, cy - amp, cz},
      {cx, cy, cz + amp},
      {cx, cy, cz - amp},
  };

  const int n = (MagCalibration::kMinSamples / 6 + 1) * 6 + 6;
  for (int i = 0; i < n; ++i) {
    cal.FeedSample(faces[i % 6]);
  }

  cal.Finish();
  EXPECT_EQ(cal.GetStatus(), MagCalibStatus::Done);
  EXPECT_TRUE(cal.IsValid());

  const auto& d = cal.GetData();
  EXPECT_NEAR(d.offset[0], cx, 0.1f);
  EXPECT_NEAR(d.offset[1], cy, 0.1f);
  EXPECT_NEAR(d.offset[2], cz, 0.1f);
}

TEST(MagCalibration, ApplySubtractsOffset) {
  MagCalibData data;
  data.offset[0] = 10.f;
  data.offset[1] = -20.f;
  data.offset[2] = 5.f;
  data.valid = true;

  MagCalibration cal;
  cal.SetData(data);

  MagData m{110.f, -120.f, 55.f};
  cal.Apply(m);

  EXPECT_FLOAT_EQ(m.mx, 100.f);
  EXPECT_FLOAT_EQ(m.my, -100.f);
  EXPECT_FLOAT_EQ(m.mz, 50.f);
}

TEST(MagCalibration, ApplyNoopWhenInvalid) {
  MagCalibration cal;  // no SetData → not valid

  MagData m{100.f, 200.f, 300.f};
  cal.Apply(m);

  // Should be unchanged
  EXPECT_FLOAT_EQ(m.mx, 100.f);
  EXPECT_FLOAT_EQ(m.my, 200.f);
  EXPECT_FLOAT_EQ(m.mz, 300.f);
}

TEST(MagCalibration, CancelResetsToIdle) {
  MagCalibration cal;
  cal.Start();
  FeedSphereSamples(cal, MagCalibration::kMinSamples + 10);
  cal.Cancel();

  EXPECT_EQ(cal.GetStatus(), MagCalibStatus::Idle);
  EXPECT_FALSE(cal.IsCollecting());
}

TEST(MagCalibration, SetDataRestoresValidCalib) {
  MagCalibData data;
  data.offset[0] = 1.f;
  data.offset[1] = 2.f;
  data.offset[2] = 3.f;
  data.valid = true;

  MagCalibration cal;
  cal.SetData(data);

  EXPECT_TRUE(cal.IsValid());
  EXPECT_EQ(cal.GetStatus(), MagCalibStatus::Done);
  EXPECT_FLOAT_EQ(cal.GetData().offset[0], 1.f);
  EXPECT_FLOAT_EQ(cal.GetData().offset[1], 2.f);
  EXPECT_FLOAT_EQ(cal.GetData().offset[2], 3.f);
}

TEST(MagCalibration, FinishSetsValidFlag) {
  MagCalibration cal;
  cal.Start();
  FeedSphereSamples(cal, MagCalibration::kMinSamples + 30);
  cal.Finish();

  EXPECT_EQ(cal.GetStatus(), MagCalibStatus::Done);
  EXPECT_TRUE(cal.GetData().valid);
}

TEST(MagCalibration, FeedSampleIgnoredWhenNotCollecting) {
  MagCalibration cal;
  // Start → Finish (too few samples) → Failed
  cal.Start();
  cal.FeedSample({400.f, 0.f, 0.f});
  cal.Finish();
  EXPECT_EQ(cal.GetStatus(), MagCalibStatus::Failed);

  // Feed after Failed should not crash or change state
  cal.FeedSample({400.f, 0.f, 0.f});
  EXPECT_EQ(cal.GetStatus(), MagCalibStatus::Failed);
}

TEST(MagCalibration, FinishIgnoredWhenNotCollecting) {
  MagCalibration cal;
  // Idle → Finish should do nothing
  cal.Finish();
  EXPECT_EQ(cal.GetStatus(), MagCalibStatus::Idle);
}
