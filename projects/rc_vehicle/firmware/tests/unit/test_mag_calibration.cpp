#include <gtest/gtest.h>

#include <cmath>

#include "mag_calibration.hpp"

// ─── Helpers ──────────────────────────────────────────────────────────────────

static constexpr float kPi = 3.14159265358979323846f;

/**
 * Feed circle samples in the XY plane (Z ≈ 0) with optional hard iron offset.
 * Simulates a yaw rotation with sensor mounted horizontally (normal ≈ Z).
 */
static void FeedCircleSamplesXY(MagCalibration& cal, int count,
                                 float radius = 400.f,
                                 float cx = 0.f, float cy = 0.f, float cz = 0.f) {
  for (int i = 0; i < count; ++i) {
    const float angle = 2.f * kPi * static_cast<float>(i) / static_cast<float>(count);
    MagData m;
    m.mx = cx + radius * std::cos(angle);
    m.my = cy + radius * std::sin(angle);
    m.mz = cz;  // vertical component — constant during yaw rotation
    cal.FeedSample(m);
  }
}

/**
 * Feed circle samples in a tilted plane.
 * Normal vector defines the rotation axis (e.g. sensor Z at some tilt).
 * Simulates a yaw rotation with sensor mounted at arbitrary angle.
 */
static void FeedCircleSamplesTilted(MagCalibration& cal, int count,
                                     float radius, float cx, float cy, float cz,
                                     float nx, float ny, float nz) {
  // Build orthonormal basis in the plane perpendicular to (nx, ny, nz)
  float aux[3] = {0.f, 0.f, 1.f};
  const float dot = nx * aux[0] + ny * aux[1] + nz * aux[2];
  if (std::fabs(dot) > 0.9f) {
    aux[0] = 1.f; aux[1] = 0.f; aux[2] = 0.f;
  }
  // e1 = normalize(n × aux)
  float e1[3] = {
    ny * aux[2] - nz * aux[1],
    nz * aux[0] - nx * aux[2],
    nx * aux[1] - ny * aux[0],
  };
  const float len1 = std::sqrt(e1[0]*e1[0] + e1[1]*e1[1] + e1[2]*e1[2]);
  e1[0] /= len1; e1[1] /= len1; e1[2] /= len1;
  // e2 = n × e1
  float e2[3] = {
    ny * e1[2] - nz * e1[1],
    nz * e1[0] - nx * e1[2],
    nx * e1[1] - ny * e1[0],
  };

  for (int i = 0; i < count; ++i) {
    const float angle = 2.f * kPi * static_cast<float>(i) / static_cast<float>(count);
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    MagData m;
    m.mx = cx + radius * (c * e1[0] + s * e2[0]);
    m.my = cy + radius * (c * e1[1] + s * e2[1]);
    m.mz = cz + radius * (c * e1[2] + s * e2[2]);
    cal.FeedSample(m);
  }
}

/**
 * Feed 3D sphere samples (all axes vary equally) — NOT planar.
 */
static void FeedSphereSamples(MagCalibration& cal, int count,
                               float amplitude = 400.f) {
  const MagData faces[6] = {
      {amplitude, 0.f, 0.f},  {-amplitude, 0.f, 0.f},
      {0.f, amplitude, 0.f},  {0.f, -amplitude, 0.f},
      {0.f, 0.f, amplitude},  {0.f, 0.f, -amplitude},
  };
  for (int i = 0; i < count; ++i) {
    cal.FeedSample(faces[i % 6]);
  }
}

// ─── Basic state machine tests ───────────────────────────────────────────────

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

TEST(MagCalibration, CancelResetsToIdle) {
  MagCalibration cal;
  cal.Start();
  FeedCircleSamplesXY(cal, MagCalibration::kMinSamples + 10);
  cal.Cancel();
  EXPECT_EQ(cal.GetStatus(), MagCalibStatus::Idle);
  EXPECT_FALSE(cal.IsCollecting());
}

TEST(MagCalibration, FinishIgnoredWhenNotCollecting) {
  MagCalibration cal;
  cal.Finish();
  EXPECT_EQ(cal.GetStatus(), MagCalibStatus::Idle);
}

TEST(MagCalibration, FeedSampleIgnoredWhenNotCollecting) {
  MagCalibration cal;
  cal.Start();
  cal.FeedSample({400.f, 0.f, 0.f});
  cal.Finish();
  EXPECT_EQ(cal.GetStatus(), MagCalibStatus::Failed);
  cal.FeedSample({400.f, 0.f, 0.f});
  EXPECT_EQ(cal.GetStatus(), MagCalibStatus::Failed);
}

// ─── Failure reasons ─────────────────────────────────────────────────────────

TEST(MagCalibration, FinishFailsWithTooFewSamples) {
  MagCalibration cal;
  cal.Start();
  cal.FeedSample({400.f, 0.f, 0.f});
  cal.Finish();
  EXPECT_EQ(cal.GetStatus(), MagCalibStatus::Failed);
  EXPECT_EQ(cal.GetFailReason(), MagCalibFailReason::TooFewSamples);
  EXPECT_FALSE(cal.IsValid());
}

TEST(MagCalibration, FinishFailsIfRadiusTooSmall) {
  MagCalibration cal;
  cal.Start();
  const MagData tiny{1.f, 1.f, 1.f};
  for (int i = 0; i < MagCalibration::kMinSamples + 10; ++i) {
    cal.FeedSample(tiny);
  }
  cal.Finish();
  EXPECT_EQ(cal.GetStatus(), MagCalibStatus::Failed);
  EXPECT_EQ(cal.GetFailReason(), MagCalibFailReason::RadiusTooSmall);
}

TEST(MagCalibration, FinishFailsIfNotPlanar) {
  MagCalibration cal;
  cal.Start();
  // Sphere samples: all 3 axes vary equally → not planar
  FeedSphereSamples(cal, MagCalibration::kMinSamples + 30);
  cal.Finish();
  EXPECT_EQ(cal.GetStatus(), MagCalibStatus::Failed);
  EXPECT_EQ(cal.GetFailReason(), MagCalibFailReason::NotPlanar);
}

TEST(MagCalibration, FailReasonStrings) {
  MagCalibration cal;

  cal.Start();
  cal.FeedSample({400.f, 0.f, 0.f});
  cal.Finish();
  EXPECT_STREQ(cal.GetFailReasonStr(), "too_few_samples");

  cal.Start();
  for (int i = 0; i < MagCalibration::kMinSamples + 10; ++i) {
    cal.FeedSample({1.f, 1.f, 1.f});
  }
  cal.Finish();
  EXPECT_STREQ(cal.GetFailReasonStr(), "radius_too_small");

  cal.Start();
  FeedSphereSamples(cal, MagCalibration::kMinSamples + 30);
  cal.Finish();
  EXPECT_STREQ(cal.GetFailReasonStr(), "not_planar");
}

// ─── Successful calibration ──────────────────────────────────────────────────

TEST(MagCalibration, FinishSucceedsWithCircleXY) {
  MagCalibration cal;
  cal.Start();

  const float cx = 50.f, cy = -30.f, cz = 20.f;
  FeedCircleSamplesXY(cal, MagCalibration::kMinSamples + 60, 400.f, cx, cy, cz);

  cal.Finish();
  EXPECT_EQ(cal.GetStatus(), MagCalibStatus::Done);
  EXPECT_TRUE(cal.IsValid());

  const auto& d = cal.GetData();
  // Hard iron offset should match the circle centre
  EXPECT_NEAR(d.offset[0], cx, 5.f);
  EXPECT_NEAR(d.offset[1], cy, 5.f);
  // Z offset: cz (constant), min/max approach matches
  EXPECT_NEAR(d.offset[2], cz, 5.f);

  // Normal should be close to ±Z (circle in XY plane)
  EXPECT_NEAR(std::fabs(d.normal[2]), 1.f, 0.05f);
  EXPECT_NEAR(d.normal[0], 0.f, 0.05f);
  EXPECT_NEAR(d.normal[1], 0.f, 0.05f);
}

TEST(MagCalibration, NormalCorrectForTiltedSensor) {
  MagCalibration cal;
  cal.Start();

  // Sensor tilted 30° around Y: normal in sensor frame ≈ (-sin30, 0, cos30)
  const float tilt = 30.f * kPi / 180.f;
  const float nx = -std::sin(tilt);
  const float ny = 0.f;
  const float nz = std::cos(tilt);

  FeedCircleSamplesTilted(cal, 500, 400.f, 0.f, 0.f, 0.f, nx, ny, nz);

  cal.Finish();
  EXPECT_EQ(cal.GetStatus(), MagCalibStatus::Done);

  const auto& d = cal.GetData();
  // Normal may be ±(nx,ny,nz) — sign ambiguity from eigenvector
  const float dot = d.normal[0]*nx + d.normal[1]*ny + d.normal[2]*nz;
  EXPECT_NEAR(std::fabs(dot), 1.f, 0.05f);
}

TEST(MagCalibration, NormalCorrectForVerticalSensor) {
  MagCalibration cal;
  cal.Start();

  // PCB vertical: sensor Z is horizontal, rotation axis ≈ sensor X
  const float nx = 1.f, ny = 0.f, nz = 0.f;
  FeedCircleSamplesTilted(cal, 500, 300.f, 50.f, -20.f, 10.f, nx, ny, nz);

  cal.Finish();
  EXPECT_EQ(cal.GetStatus(), MagCalibStatus::Done);

  const auto& d = cal.GetData();
  const float dot = d.normal[0]*nx + d.normal[1]*ny + d.normal[2]*nz;
  EXPECT_NEAR(std::fabs(dot), 1.f, 0.05f);

  // Hard iron offset
  EXPECT_NEAR(d.offset[0], 50.f, 5.f);
  EXPECT_NEAR(d.offset[1], -20.f, 5.f);
  EXPECT_NEAR(d.offset[2], 10.f, 5.f);
}

// ─── Basis orthonormality ────────────────────────────────────────────────────

TEST(MagCalibration, BasisIsOrthonormal) {
  MagCalibration cal;
  cal.Start();
  FeedCircleSamplesXY(cal, 500, 400.f);
  cal.Finish();
  ASSERT_TRUE(cal.IsValid());

  const auto& d = cal.GetData();

  // basis1 · basis2 ≈ 0
  const float b1b2 = d.basis1[0]*d.basis2[0] + d.basis1[1]*d.basis2[1] + d.basis1[2]*d.basis2[2];
  EXPECT_NEAR(b1b2, 0.f, 1e-5f);

  // basis1 · normal ≈ 0
  const float b1n = d.basis1[0]*d.normal[0] + d.basis1[1]*d.normal[1] + d.basis1[2]*d.normal[2];
  EXPECT_NEAR(b1n, 0.f, 1e-5f);

  // basis2 · normal ≈ 0
  const float b2n = d.basis2[0]*d.normal[0] + d.basis2[1]*d.normal[1] + d.basis2[2]*d.normal[2];
  EXPECT_NEAR(b2n, 0.f, 1e-5f);

  // |basis1| ≈ 1
  const float len1 = std::sqrt(d.basis1[0]*d.basis1[0] + d.basis1[1]*d.basis1[1] + d.basis1[2]*d.basis1[2]);
  EXPECT_NEAR(len1, 1.f, 1e-5f);

  // |basis2| ≈ 1
  const float len2 = std::sqrt(d.basis2[0]*d.basis2[0] + d.basis2[1]*d.basis2[1] + d.basis2[2]*d.basis2[2]);
  EXPECT_NEAR(len2, 1.f, 1e-5f);
}

// ─── Apply / SetData ─────────────────────────────────────────────────────────

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
  MagCalibration cal;
  MagData m{100.f, 200.f, 300.f};
  cal.Apply(m);
  EXPECT_FLOAT_EQ(m.mx, 100.f);
  EXPECT_FLOAT_EQ(m.my, 200.f);
  EXPECT_FLOAT_EQ(m.mz, 300.f);
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
}

TEST(MagCalibration, SetDataRecomputesBasis) {
  MagCalibData data{};
  data.normal[0] = 1.f;  // normal = X axis
  data.normal[1] = 0.f;
  data.normal[2] = 0.f;
  data.valid = true;

  MagCalibration cal;
  cal.SetData(data);

  const auto& d = cal.GetData();
  // basis1 and basis2 should be in the YZ plane
  EXPECT_NEAR(d.basis1[0], 0.f, 1e-5f);
  EXPECT_NEAR(d.basis2[0], 0.f, 1e-5f);
  // And be orthonormal
  const float b1b2 = d.basis1[0]*d.basis2[0] + d.basis1[1]*d.basis2[1] + d.basis1[2]*d.basis2[2];
  EXPECT_NEAR(b1b2, 0.f, 1e-5f);
}

// ─── Heading via PCA projection ──────────────────────────────────────────────

TEST(MagCalibration, HeadingViaProjection) {
  // Calibrate with XY circle
  MagCalibration cal;
  cal.Start();
  FeedCircleSamplesXY(cal, 500, 400.f);
  cal.Finish();
  ASSERT_TRUE(cal.IsValid());

  const auto& d = cal.GetData();

  // Simulate a mag reading at 0° (should give consistent angle)
  MagData m0{400.f, 0.f, 0.f};
  cal.Apply(m0);

  const float dot_n0 = m0.mx*d.normal[0] + m0.my*d.normal[1] + m0.mz*d.normal[2];
  const float px0 = m0.mx - dot_n0*d.normal[0];
  const float py0 = m0.my - dot_n0*d.normal[1];
  const float pz0 = m0.mz - dot_n0*d.normal[2];
  const float c1_0 = px0*d.basis1[0] + py0*d.basis1[1] + pz0*d.basis1[2];
  const float c2_0 = px0*d.basis2[0] + py0*d.basis2[1] + pz0*d.basis2[2];
  const float h0 = std::atan2(c2_0, c1_0) * 180.f / kPi;

  // Simulate a mag reading at 90° (rotated 90° CCW in XY)
  MagData m90{0.f, 400.f, 0.f};
  cal.Apply(m90);

  const float dot_n90 = m90.mx*d.normal[0] + m90.my*d.normal[1] + m90.mz*d.normal[2];
  const float px90 = m90.mx - dot_n90*d.normal[0];
  const float py90 = m90.my - dot_n90*d.normal[1];
  const float pz90 = m90.mz - dot_n90*d.normal[2];
  const float c1_90 = px90*d.basis1[0] + py90*d.basis1[1] + pz90*d.basis1[2];
  const float c2_90 = px90*d.basis2[0] + py90*d.basis2[1] + pz90*d.basis2[2];
  const float h90 = std::atan2(c2_90, c1_90) * 180.f / kPi;

  // Difference should be ~90°
  float delta = h90 - h0;
  if (delta > 180.f) delta -= 360.f;
  if (delta < -180.f) delta += 360.f;
  EXPECT_NEAR(std::fabs(delta), 90.f, 2.f);
}
