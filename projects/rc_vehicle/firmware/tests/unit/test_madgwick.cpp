#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <vector>

#include "imu_calibration.hpp"
#include "madgwick_filter.hpp"
#include "mpu6050_spi.hpp"
#include "test_helpers.hpp"

using namespace rc_vehicle;
using namespace rc_vehicle::testing;

// ═══════════════════════════════════════════════════════════════════════════
// Initialization Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(MadgwickTest, InitialQuaternionIsIdentity) {
  MadgwickFilter filter;
  float qw, qx, qy, qz;
  filter.GetQuaternion(qw, qx, qy, qz);

  EXPECT_FLOAT_EQ(qw, 1.0f) << "Initial qw should be 1.0 (identity quaternion)";
  EXPECT_FLOAT_EQ(qx, 0.0f) << "Initial qx should be 0.0";
  EXPECT_FLOAT_EQ(qy, 0.0f) << "Initial qy should be 0.0";
  EXPECT_FLOAT_EQ(qz, 0.0f) << "Initial qz should be 0.0";
}

TEST(MadgwickTest, InitialEulerAnglesAreZero) {
  MadgwickFilter filter;
  float pitch, roll, yaw;
  filter.GetEulerRad(pitch, roll, yaw);

  EXPECT_NEAR(pitch, 0.0f, 1e-5f) << "Initial pitch should be ~0";
  EXPECT_NEAR(roll, 0.0f, 1e-5f) << "Initial roll should be ~0";
  EXPECT_NEAR(yaw, 0.0f, 1e-5f) << "Initial yaw should be ~0";
}

// ═══════════════════════════════════════════════════════════════════════════
// Quaternion Normalization Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(MadgwickTest, QuaternionStaysNormalized) {
  MadgwickFilter filter;

  // Simulate some updates with typical IMU data
  for (int i = 0; i < 100; ++i) {
    filter.Update(0.0f, 0.0f, 1.0f,  // accel (1g down in Z)
                  0.1f, 0.0f, 0.0f,  // gyro (small rotation around X)
                  0.01f);            // dt = 10ms
  }

  float qw, qx, qy, qz;
  filter.GetQuaternion(qw, qx, qy, qz);

  float norm = std::sqrt(qw * qw + qx * qx + qy * qy + qz * qz);
  EXPECT_NEAR(norm, 1.0f, 1e-5f)
      << "Quaternion should remain normalized after updates";
}

TEST(MadgwickTest, QuaternionNormalizedAfterManyUpdates) {
  MadgwickFilter filter;

  // Many updates with varying data
  for (int i = 0; i < 1000; ++i) {
    float t = i * 0.01f;
    filter.Update(std::sin(t) * 0.1f, std::cos(t) * 0.1f,
                  1.0f,                        // varying accel
                  std::sin(t * 2.0f) * 10.0f,  // varying gyro
                  std::cos(t * 2.0f) * 10.0f, 0.0f, 0.01f);
  }

  float qw, qx, qy, qz;
  filter.GetQuaternion(qw, qx, qy, qz);

  EXPECT_TRUE(IsQuaternionNormalized(qw, qx, qy, qz))
      << "Quaternion should remain normalized after many updates";
}

// ═══════════════════════════════════════════════════════════════════════════
// Gravity Alignment Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(MadgwickTest, ConvergesToGravityDirection) {
  MadgwickFilter filter;
  filter.SetBeta(0.5f);  // Higher beta for faster convergence

  // Simulate IMU at rest with gravity pointing down (0, 0, 1g)
  for (int i = 0; i < 200; ++i) {
    filter.Update(0.0f, 0.0f, 1.0f,  // accel: 1g down
                  0.0f, 0.0f, 0.0f,  // gyro: no rotation
                  0.01f);
  }

  float pitch, roll, yaw;
  filter.GetEulerRad(pitch, roll, yaw);

  // With gravity down and no rotation, pitch and roll should be near zero
  EXPECT_NEAR(pitch, 0.0f, 0.1f)
      << "Pitch should converge to 0 with gravity down";
  EXPECT_NEAR(roll, 0.0f, 0.1f)
      << "Roll should converge to 0 with gravity down";
}

TEST(MadgwickTest, DetectsTilt) {
  MadgwickFilter filter;
  filter.SetBeta(0.5f);

  // Simulate IMU tilted 45 degrees around X axis
  // Gravity vector rotated: (0, sin(45°), cos(45°)) ≈ (0, 0.707, 0.707)
  for (int i = 0; i < 200; ++i) {
    filter.Update(0.0f, 0.707f, 0.707f,  // tilted gravity
                  0.0f, 0.0f, 0.0f,      // no rotation
                  0.01f);
  }

  float pitch, roll, yaw;
  filter.GetEulerRad(pitch, roll, yaw);

  // Should detect ~45 degree roll
  EXPECT_NEAR(roll, M_PI / 4.0f, 0.2f)
      << "Should detect 45 degree tilt around X axis";
}

// ═══════════════════════════════════════════════════════════════════════════
// Gyroscope Integration Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(MadgwickTest, IntegratesGyroRotation) {
  MadgwickFilter filter;

  // Constant rotation around Z axis at 10 deg/s for 1 second
  float rotation_rate = 10.0f;  // deg/s
  float dt = 0.01f;             // 10ms
  int steps = 100;              // 1 second total

  for (int i = 0; i < steps; ++i) {
    filter.Update(0.0f, 0.0f, 1.0f,  // gravity down
                  0.0f, 0.0f, rotation_rate, dt);
  }

  float pitch, roll, yaw;
  filter.GetEulerDeg(pitch, roll, yaw);

  // After 1 second at 10 deg/s, yaw should be ~10 degrees
  // (may have some error due to filter dynamics)
  EXPECT_NEAR(yaw, 10.0f, 5.0f)
      << "Yaw should integrate gyro rotation (with some tolerance)";
}

// ═══════════════════════════════════════════════════════════════════════════
// Beta Parameter Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(MadgwickTest, BetaParameterGetSet) {
  MadgwickFilter filter;

  EXPECT_FLOAT_EQ(filter.GetBeta(), 0.1f) << "Default beta should be 0.1";

  filter.SetBeta(0.5f);
  EXPECT_FLOAT_EQ(filter.GetBeta(), 0.5f) << "Beta should be updated to 0.5";
}

TEST(MadgwickTest, HigherBetaFasterConvergence) {
  MadgwickFilter filter_slow, filter_fast;
  filter_slow.SetBeta(0.01f);  // Slow convergence
  filter_fast.SetBeta(0.5f);   // Fast convergence

  // Apply same tilted gravity to both
  for (int i = 0; i < 50; ++i) {
    filter_slow.Update(0.0f, 0.707f, 0.707f, 0.0f, 0.0f, 0.0f, 0.01f);
    filter_fast.Update(0.0f, 0.707f, 0.707f, 0.0f, 0.0f, 0.0f, 0.01f);
  }

  float pitch_slow, roll_slow, yaw_slow;
  float pitch_fast, roll_fast, yaw_fast;
  filter_slow.GetEulerRad(pitch_slow, roll_slow, yaw_slow);
  filter_fast.GetEulerRad(pitch_fast, roll_fast, yaw_fast);

  // Fast filter should be closer to target (45 degrees = π/4)
  float error_slow = std::abs(roll_slow - M_PI / 4.0f);
  float error_fast = std::abs(roll_fast - M_PI / 4.0f);

  EXPECT_LT(error_fast, error_slow)
      << "Higher beta should converge faster to target orientation";
}

// ═══════════════════════════════════════════════════════════════════════════
// Reset Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(MadgwickTest, ResetToIdentity) {
  MadgwickFilter filter;

  // Apply some updates
  for (int i = 0; i < 100; ++i) {
    filter.Update(0.5f, 0.5f, 0.707f, 10.0f, 5.0f, 2.0f, 0.01f);
  }

  // Verify it's not identity
  float qw, qx, qy, qz;
  filter.GetQuaternion(qw, qx, qy, qz);
  EXPECT_FALSE(qw == 1.0f && qx == 0.0f && qy == 0.0f && qz == 0.0f)
      << "Quaternion should have changed after updates";

  // Reset
  filter.Reset();
  filter.GetQuaternion(qw, qx, qy, qz);

  EXPECT_FLOAT_EQ(qw, 1.0f) << "After reset, qw should be 1.0";
  EXPECT_FLOAT_EQ(qx, 0.0f) << "After reset, qx should be 0.0";
  EXPECT_FLOAT_EQ(qy, 0.0f) << "After reset, qy should be 0.0";
  EXPECT_FLOAT_EQ(qz, 0.0f) << "After reset, qz should be 0.0";
}

// ═══════════════════════════════════════════════════════════════════════════
// ImuData Overload Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(MadgwickTest, UpdateWithImuData) {
  MadgwickFilter filter;

  ImuData imu = MakeImuData(0.f, 0.f, 1.f,   // 1g down
                            0.f, 0.f, 0.f);  // no rotation

  // Update using ImuData overload
  for (int i = 0; i < 100; ++i) {
    filter.Update(imu, 0.01f);
  }

  float pitch, roll, yaw;
  filter.GetEulerRad(pitch, roll, yaw);

  EXPECT_NEAR(pitch, 0.0f, 0.1f)
      << "Pitch should be near 0 with ImuData update";
  EXPECT_NEAR(roll, 0.0f, 0.1f) << "Roll should be near 0 with ImuData update";
}

// ═══════════════════════════════════════════════════════════════════════════
// Edge Cases
// ═══════════════════════════════════════════════════════════════════════════

TEST(MadgwickTest, ZeroAcceleration) {
  MadgwickFilter filter;

  // Update with zero acceleration (shouldn't crash)
  for (int i = 0; i < 10; ++i) {
    filter.Update(0.0f, 0.0f, 0.0f,  // zero accel
                  1.0f, 0.0f, 0.0f,  // some gyro
                  0.01f);
  }

  float qw, qx, qy, qz;
  filter.GetQuaternion(qw, qx, qy, qz);

  // Should still have a valid normalized quaternion
  EXPECT_TRUE(IsQuaternionNormalized(qw, qx, qy, qz))
      << "Quaternion should remain valid with zero acceleration";
}

TEST(MadgwickTest, VerySmallDt) {
  MadgwickFilter filter;

  // Update with very small dt
  filter.Update(0.0f, 0.0f, 1.0f, 10.0f, 0.0f, 0.0f, 0.0001f);  // 0.1ms

  float qw, qx, qy, qz;
  filter.GetQuaternion(qw, qx, qy, qz);

  EXPECT_TRUE(IsQuaternionNormalized(qw, qx, qy, qz))
      << "Should handle very small dt";
}

TEST(MadgwickTest, LargeDt) {
  MadgwickFilter filter;

  // Update with large dt
  filter.Update(0.0f, 0.0f, 1.0f, 10.0f, 0.0f, 0.0f, 1.0f);  // 1 second

  float qw, qx, qy, qz;
  filter.GetQuaternion(qw, qx, qy, qz);

  EXPECT_TRUE(IsQuaternionNormalized(qw, qx, qy, qz))
      << "Should handle large dt";
}

// ═══════════════════════════════════════════════════════════════════════════
// Euler Angle Conversion Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(MadgwickTest, EulerRadToDegConversion) {
  MadgwickFilter filter;
  filter.SetBeta(0.5f);

  // Apply rotation to get non-zero angles
  for (int i = 0; i < 100; ++i) {
    filter.Update(0.0f, 0.5f, 0.866f,  // ~30 degree tilt
                  0.0f, 0.0f, 0.0f, 0.01f);
  }

  float pitch_rad, roll_rad, yaw_rad;
  float pitch_deg, roll_deg, yaw_deg;

  filter.GetEulerRad(pitch_rad, roll_rad, yaw_rad);
  filter.GetEulerDeg(pitch_deg, roll_deg, yaw_deg);

  // Verify conversion: degrees = radians * 180/π
  constexpr float kRadToDeg = 57.295779513f;
  EXPECT_NEAR(pitch_deg, pitch_rad * kRadToDeg, 0.01f)
      << "Pitch conversion rad->deg should be accurate";
  EXPECT_NEAR(roll_deg, roll_rad * kRadToDeg, 0.01f)
      << "Roll conversion rad->deg should be accurate";
  EXPECT_NEAR(yaw_deg, yaw_rad * kRadToDeg, 0.01f)
      << "Yaw conversion rad->deg should be accurate";
}

TEST(MadgwickTest, EulerAnglesInValidRange) {
  MadgwickFilter filter;

  // Apply various rotations
  for (int i = 0; i < 200; ++i) {
    float t = i * 0.01f;
    filter.Update(std::sin(t) * 0.2f, std::cos(t) * 0.2f, 0.9f,
                  std::sin(t * 3.0f) * 20.0f, std::cos(t * 3.0f) * 20.0f,
                  std::sin(t * 2.0f) * 15.0f, 0.01f);
  }

  float pitch, roll, yaw;
  filter.GetEulerRad(pitch, roll, yaw);

  // Pitch should be in [-π/2, π/2]
  EXPECT_GE(pitch, -M_PI / 2.0f) << "Pitch should be >= -π/2";
  EXPECT_LE(pitch, M_PI / 2.0f) << "Pitch should be <= π/2";

  // Roll should be in [-π, π]
  EXPECT_GE(roll, -M_PI) << "Roll should be >= -π";
  EXPECT_LE(roll, M_PI) << "Roll should be <= π";

  // Yaw should be in [-π, π]
  EXPECT_GE(yaw, -M_PI) << "Yaw should be >= -π";
  EXPECT_LE(yaw, M_PI) << "Yaw should be <= π";
}

// ═══════════════════════════════════════════════════════════════════════════
// Vehicle Frame Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(MadgwickTest, SetVehicleFrameWithValidVectors) {
  MadgwickFilter filter;

  // Define vehicle frame: gravity down (0,0,1), forward (1,0,0)
  float gravity[3] = {0.0f, 0.0f, 1.0f};
  float forward[3] = {1.0f, 0.0f, 0.0f};

  filter.SetVehicleFrame(gravity, forward, true);

  // After setting vehicle frame, quaternion should still be normalized
  float qw, qx, qy, qz;
  filter.GetQuaternion(qw, qx, qy, qz);

  EXPECT_TRUE(IsQuaternionNormalized(qw, qx, qy, qz))
      << "Quaternion should remain normalized after SetVehicleFrame";
}

TEST(MadgwickTest, SetVehicleFrameWithInvalidFlag) {
  MadgwickFilter filter;

  float gravity[3] = {0.0f, 0.0f, 1.0f};
  float forward[3] = {1.0f, 0.0f, 0.0f};

  // Set with valid=false should not use vehicle frame
  filter.SetVehicleFrame(gravity, forward, false);

  float qw, qx, qy, qz;
  filter.GetQuaternion(qw, qx, qy, qz);

  // Should still return identity quaternion (no updates yet)
  EXPECT_FLOAT_EQ(qw, 1.0f);
  EXPECT_FLOAT_EQ(qx, 0.0f);
  EXPECT_FLOAT_EQ(qy, 0.0f);
  EXPECT_FLOAT_EQ(qz, 0.0f);
}

TEST(MadgwickTest, SetVehicleFrameWithNullForward) {
  MadgwickFilter filter;

  float gravity[3] = {0.0f, 0.0f, 1.0f};

  // Null forward vector should be handled gracefully
  filter.SetVehicleFrame(gravity, nullptr, true);

  float qw, qx, qy, qz;
  filter.GetQuaternion(qw, qx, qy, qz);

  // Should still have valid quaternion
  EXPECT_TRUE(IsQuaternionNormalized(qw, qx, qy, qz));
}

TEST(MadgwickTest, SetVehicleFrameWithZeroForward) {
  MadgwickFilter filter;

  float gravity[3] = {0.0f, 0.0f, 1.0f};
  float forward[3] = {0.0f, 0.0f, 0.0f};  // Zero vector

  // Should handle zero forward vector gracefully
  filter.SetVehicleFrame(gravity, forward, true);

  float qw, qx, qy, qz;
  filter.GetQuaternion(qw, qx, qy, qz);

  EXPECT_TRUE(IsQuaternionNormalized(qw, qx, qy, qz));
}

TEST(MadgwickTest, VehicleFrameWithDifferentOrientations) {
  MadgwickFilter filter;

  // Test with forward pointing in different directions
  float gravity[3] = {0.0f, 0.0f, 1.0f};
  float forward_x[3] = {1.0f, 0.0f, 0.0f};
  float forward_y[3] = {0.0f, 1.0f, 0.0f};
  float forward_diag[3] = {0.707f, 0.707f, 0.0f};

  // Each should work without crashing
  filter.SetVehicleFrame(gravity, forward_x, true);
  float qw1, qx1, qy1, qz1;
  filter.GetQuaternion(qw1, qx1, qy1, qz1);
  EXPECT_TRUE(IsQuaternionNormalized(qw1, qx1, qy1, qz1));

  filter.SetVehicleFrame(gravity, forward_y, true);
  float qw2, qx2, qy2, qz2;
  filter.GetQuaternion(qw2, qx2, qy2, qz2);
  EXPECT_TRUE(IsQuaternionNormalized(qw2, qx2, qy2, qz2));

  filter.SetVehicleFrame(gravity, forward_diag, true);
  float qw3, qx3, qy3, qz3;
  filter.GetQuaternion(qw3, qx3, qy3, qz3);
  EXPECT_TRUE(IsQuaternionNormalized(qw3, qx3, qy3, qz3));
}

// ═══════════════════════════════════════════════════════════════════════════
// dt Parameter Edge Cases
// ═══════════════════════════════════════════════════════════════════════════

TEST(MadgwickTest, ZeroDt) {
  MadgwickFilter filter;

  float qw_before, qx_before, qy_before, qz_before;
  filter.GetQuaternion(qw_before, qx_before, qy_before, qz_before);

  // Update with dt=0 should not change quaternion
  filter.Update(0.0f, 0.0f, 1.0f, 10.0f, 0.0f, 0.0f, 0.0f);

  float qw_after, qx_after, qy_after, qz_after;
  filter.GetQuaternion(qw_after, qx_after, qy_after, qz_after);

  EXPECT_FLOAT_EQ(qw_before, qw_after)
      << "Quaternion should not change with dt=0";
  EXPECT_FLOAT_EQ(qx_before, qx_after);
  EXPECT_FLOAT_EQ(qy_before, qy_after);
  EXPECT_FLOAT_EQ(qz_before, qz_after);
}

TEST(MadgwickTest, NegativeDt) {
  MadgwickFilter filter;

  float qw_before, qx_before, qy_before, qz_before;
  filter.GetQuaternion(qw_before, qx_before, qy_before, qz_before);

  // Negative dt should be ignored (treated as invalid)
  filter.Update(0.0f, 0.0f, 1.0f, 10.0f, 0.0f, 0.0f, -0.01f);

  float qw_after, qx_after, qy_after, qz_after;
  filter.GetQuaternion(qw_after, qx_after, qy_after, qz_after);

  EXPECT_FLOAT_EQ(qw_before, qw_after)
      << "Quaternion should not change with negative dt";
}

TEST(MadgwickTest, VeryLargeDt) {
  MadgwickFilter filter;

  // Very large dt (10 seconds) should still produce valid quaternion
  filter.Update(0.0f, 0.0f, 1.0f, 100.0f, 0.0f, 0.0f, 10.0f);

  float qw, qx, qy, qz;
  filter.GetQuaternion(qw, qx, qy, qz);

  EXPECT_TRUE(IsQuaternionNormalized(qw, qx, qy, qz))
      << "Should handle very large dt without numerical issues";
}

// ═══════════════════════════════════════════════════════════════════════════
// Numerical Stability Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(MadgwickTest, VerySmallAcceleration) {
  MadgwickFilter filter;

  // Very small but non-zero acceleration
  for (int i = 0; i < 100; ++i) {
    filter.Update(1e-6f, 1e-6f, 1e-6f,  // tiny accel
                  1.0f, 0.0f, 0.0f,     // normal gyro
                  0.01f);
  }

  float qw, qx, qy, qz;
  filter.GetQuaternion(qw, qx, qy, qz);

  EXPECT_TRUE(IsQuaternionNormalized(qw, qx, qy, qz))
      << "Should handle very small acceleration values";
}

TEST(MadgwickTest, LargeAcceleration) {
  MadgwickFilter filter;

  // Large acceleration (e.g., during impact)
  for (int i = 0; i < 50; ++i) {
    filter.Update(10.0f, 5.0f, 20.0f,  // large accel
                  1.0f, 0.0f, 0.0f, 0.01f);
  }

  float qw, qx, qy, qz;
  filter.GetQuaternion(qw, qx, qy, qz);

  EXPECT_TRUE(IsQuaternionNormalized(qw, qx, qy, qz))
      << "Should handle large acceleration values";
}

TEST(MadgwickTest, HighGyroRates) {
  MadgwickFilter filter;

  // Very high rotation rates (e.g., 500 deg/s)
  for (int i = 0; i < 100; ++i) {
    filter.Update(0.0f, 0.0f, 1.0f, 500.0f, 300.0f, 200.0f, 0.01f);
  }

  float qw, qx, qy, qz;
  filter.GetQuaternion(qw, qx, qy, qz);

  EXPECT_TRUE(IsQuaternionNormalized(qw, qx, qy, qz))
      << "Should handle high gyro rates without instability";
}

TEST(MadgwickTest, AlternatingGyroDirection) {
  MadgwickFilter filter;

  // Rapidly alternating gyro direction
  for (int i = 0; i < 200; ++i) {
    float sign = (i % 2 == 0) ? 1.0f : -1.0f;
    filter.Update(0.0f, 0.0f, 1.0f, sign * 50.0f, 0.0f, 0.0f, 0.01f);
  }

  float qw, qx, qy, qz;
  filter.GetQuaternion(qw, qx, qy, qz);

  EXPECT_TRUE(IsQuaternionNormalized(qw, qx, qy, qz))
      << "Should handle rapidly alternating gyro input";
}

// ═══════════════════════════════════════════════════════════════════════════
// Multi-axis Rotation Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(MadgwickTest, SimultaneousMultiAxisRotation) {
  MadgwickFilter filter;
  filter.SetBeta(0.3f);

  // Rotate around all three axes simultaneously
  for (int i = 0; i < 100; ++i) {
    filter.Update(0.0f, 0.0f, 1.0f,     // gravity down
                  10.0f, 15.0f, 20.0f,  // rotation on all axes
                  0.01f);
  }

  float qw, qx, qy, qz;
  filter.GetQuaternion(qw, qx, qy, qz);

  EXPECT_TRUE(IsQuaternionNormalized(qw, qx, qy, qz))
      << "Should handle multi-axis rotation";

  // Quaternion should have changed from identity
  float quat_change =
      std::abs(qw - 1.0f) + std::abs(qx) + std::abs(qy) + std::abs(qz);
  EXPECT_GT(quat_change, 0.1f)
      << "Quaternion should have changed significantly with rotation";
}

TEST(MadgwickTest, PitchRollYawIndependence) {
  MadgwickFilter filter_pitch, filter_roll, filter_yaw;
  filter_pitch.SetBeta(0.5f);
  filter_roll.SetBeta(0.5f);
  filter_yaw.SetBeta(0.5f);

  // Pure pitch rotation (around Y)
  for (int i = 0; i < 100; ++i) {
    filter_pitch.Update(0.0f, 0.0f, 1.0f, 0.0f, 20.0f, 0.0f, 0.01f);
  }

  // Pure roll rotation (around X)
  for (int i = 0; i < 100; ++i) {
    filter_roll.Update(0.0f, 0.0f, 1.0f, 20.0f, 0.0f, 0.0f, 0.01f);
  }

  // Pure yaw rotation (around Z)
  for (int i = 0; i < 100; ++i) {
    filter_yaw.Update(0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 20.0f, 0.01f);
  }

  float p1, r1, y1, p2, r2, y2, p3, r3, y3;
  filter_pitch.GetEulerRad(p1, r1, y1);
  filter_roll.GetEulerRad(p2, r2, y2);
  filter_yaw.GetEulerRad(p3, r3, y3);

  // Pitch rotation should primarily affect pitch
  EXPECT_GT(std::abs(p1), std::abs(r1))
      << "Pitch rotation should affect pitch more than roll";

  // Roll rotation should primarily affect roll
  EXPECT_GT(std::abs(r2), std::abs(p2))
      << "Roll rotation should affect roll more than pitch";

  // Yaw rotation should primarily affect yaw
  EXPECT_GT(std::abs(y3), std::abs(p3))
      << "Yaw rotation should affect yaw more than pitch";
}

// ═══════════════════════════════════════════════════════════════════════════
// Stress and Long-Running Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(MadgwickTest, LongRunningStability) {
  MadgwickFilter filter;

  // Simulate 10 seconds of operation at 100Hz
  for (int i = 0; i < 1000; ++i) {
    float t = i * 0.01f;
    filter.Update(std::sin(t * 0.5f) * 0.1f, std::cos(t * 0.5f) * 0.1f, 1.0f,
                  std::sin(t) * 5.0f, std::cos(t) * 5.0f,
                  std::sin(t * 2.0f) * 3.0f, 0.01f);
  }

  float qw, qx, qy, qz;
  filter.GetQuaternion(qw, qx, qy, qz);

  EXPECT_TRUE(IsQuaternionNormalized(qw, qx, qy, qz))
      << "Filter should remain stable after long operation";
}

TEST(MadgwickTest, RepeatedResetAndUpdate) {
  MadgwickFilter filter;

  // Reset and update multiple times
  for (int cycle = 0; cycle < 10; ++cycle) {
    filter.Reset();

    for (int i = 0; i < 50; ++i) {
      filter.Update(0.0f, 0.0f, 1.0f, 10.0f, 5.0f, 2.0f, 0.01f);
    }

    float qw, qx, qy, qz;
    filter.GetQuaternion(qw, qx, qy, qz);

    EXPECT_TRUE(IsQuaternionNormalized(qw, qx, qy, qz))
        << "Quaternion should be normalized after cycle " << cycle;
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// Beta Parameter Boundary Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(MadgwickTest, ZeroBeta) {
  MadgwickFilter filter;
  filter.SetBeta(0.0f);

  EXPECT_FLOAT_EQ(filter.GetBeta(), 0.0f);

  // With beta=0, accelerometer correction is disabled
  // Filter should still work (gyro-only mode)
  for (int i = 0; i < 100; ++i) {
    filter.Update(0.5f, 0.5f, 0.707f,  // tilted accel (should be ignored)
                  10.0f, 0.0f, 0.0f,   // gyro rotation
                  0.01f);
  }

  float qw, qx, qy, qz;
  filter.GetQuaternion(qw, qx, qy, qz);

  EXPECT_TRUE(IsQuaternionNormalized(qw, qx, qy, qz))
      << "Filter should work with beta=0 (gyro-only mode)";
}

TEST(MadgwickTest, VeryHighBeta) {
  MadgwickFilter filter;
  filter.SetBeta(10.0f);  // Very high beta

  EXPECT_FLOAT_EQ(filter.GetBeta(), 10.0f);

  // High beta should still produce stable results
  for (int i = 0; i < 100; ++i) {
    filter.Update(0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.01f);
  }

  float qw, qx, qy, qz;
  filter.GetQuaternion(qw, qx, qy, qz);

  EXPECT_TRUE(IsQuaternionNormalized(qw, qx, qy, qz))
      << "Filter should remain stable with very high beta";
}

// ═══════════════════════════════════════════════════════════════════════════
// Adaptive Beta Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(MadgwickTest, AdaptiveBetaDefaultOff) {
  MadgwickFilter filter;
  EXPECT_FALSE(filter.GetAdaptiveBetaEnabled())
      << "Adaptive beta should be disabled by default";
}

TEST(MadgwickTest, AdaptiveBetaGetSet) {
  MadgwickFilter filter;
  filter.SetAdaptiveBeta(true, 0.3f);
  EXPECT_TRUE(filter.GetAdaptiveBetaEnabled());
  EXPECT_FLOAT_EQ(filter.GetAdaptiveThresholdG(), 0.3f);

  filter.SetAdaptiveBeta(false);
  EXPECT_FALSE(filter.GetAdaptiveBetaEnabled());
}

TEST(MadgwickTest, AdaptiveBetaNoSuppressAtRest) {
  // При покое |a| ≈ 1g: коррекция акселерометра НЕ подавляется.
  // Ожидаем, что filter с adaptive converges так же быстро, как без него.
  MadgwickFilter filter_normal, filter_adaptive;
  filter_normal.SetBeta(0.5f);
  filter_adaptive.SetBeta(0.5f);
  filter_adaptive.SetAdaptiveBeta(true, 0.2f);

  // Машина на месте, отклонена на 45° вокруг X (roll)
  for (int i = 0; i < 200; ++i) {
    filter_normal.Update(0.0f, 0.707f, 0.707f, 0.0f, 0.0f, 0.0f, 0.01f);
    filter_adaptive.Update(0.0f, 0.707f, 0.707f, 0.0f, 0.0f, 0.0f, 0.01f);
  }

  float p1, r1, y1, p2, r2, y2;
  filter_normal.GetEulerRad(p1, r1, y1);
  filter_adaptive.GetEulerRad(p2, r2, y2);

  // Оба должны сойтись к ~45°, разница незначительная
  EXPECT_NEAR(r1, r2, 0.01f)
      << "Adaptive beta at rest should converge same as normal";
  EXPECT_NEAR(std::fabs(r1), M_PI / 4.0f, 0.1f)
      << "Both filters should detect 45 degree tilt";
}

TEST(MadgwickTest, AdaptiveBetaSuppressesDuringLinearAccel) {
  // При сильном линейном ускорении |a| >> 1g: коррекция подавляется.
  // Имитируем боковое ускорение при крутом повороте (≈1.5g поперёк).
  // |a| = sqrt(1.5² + 1.0²) ≈ 1.80, deviation=0.80 > threshold=0.2 → сработает.
  MadgwickFilter filter_normal, filter_adaptive;
  filter_normal.SetBeta(0.5f);
  filter_adaptive.SetBeta(0.5f);
  filter_adaptive.SetAdaptiveBeta(true, 0.2f);

  for (int i = 0; i < 100; ++i) {
    // ay=1.5g (боковое), az=1.0g (гравитация), нет вращения
    filter_normal.Update(0.0f, 1.5f, 1.0f, 0.0f, 0.0f, 0.0f, 0.01f);
    filter_adaptive.Update(0.0f, 1.5f, 1.0f, 0.0f, 0.0f, 0.0f, 0.01f);
  }

  float p1, r1, y1, p2, r2, y2;
  filter_normal.GetEulerDeg(p1, r1, y1);
  filter_adaptive.GetEulerDeg(p2, r2, y2);

  // filter_normal тянется к ложному крену (ay >> gravity → воспринимает как
  // наклон). filter_adaptive игнорирует этот шум → меньше крен.
  float roll_diff = std::fabs(r1 - r2);
  EXPECT_GT(roll_diff, 1.0f)
      << "Adaptive beta should significantly reduce roll error during "
         "lateral acceleration (diff="
      << roll_diff << " deg)";
}

TEST(MadgwickTest, AdaptiveBetaThresholdEffect) {
  // Маленький threshold → подавляет при небольшом ускорении
  // Большой threshold → не подавляет при том же ускорении
  MadgwickFilter filter_tight, filter_loose;
  filter_tight.SetBeta(0.5f);
  filter_tight.SetAdaptiveBeta(true, 0.05f);  // очень чувствительный
  filter_loose.SetBeta(0.5f);
  filter_loose.SetAdaptiveBeta(true, 0.5f);  // менее чувствительный

  // Небольшое линейное ускорение: |a| ≈ 1.1g, deviation=0.1
  // tight: 0.1 > 0.05 → подавляет
  // loose: 0.1 < 0.5  → не подавляет
  for (int i = 0; i < 100; ++i) {
    filter_tight.Update(0.0f, 0.35f, 1.0f, 0.0f, 0.0f, 0.0f, 0.01f);
    filter_loose.Update(0.0f, 0.35f, 1.0f, 0.0f, 0.0f, 0.0f, 0.01f);
  }

  float p1, r1, y1, p2, r2, y2;
  filter_tight.GetEulerRad(p1, r1, y1);
  filter_loose.GetEulerRad(p2, r2, y2);

  // loose должен сильнее отклониться в сторону ложного крена (следит за accel)
  // tight держится на месте (гироскоп без вращения → минимальный крен)
  EXPECT_GT(std::fabs(r2), std::fabs(r1))
      << "Loose threshold should allow more roll correction than tight "
         "threshold";
}

TEST(MadgwickTest, AdaptiveBetaQuaternionStaysNormalized) {
  MadgwickFilter filter;
  filter.SetBeta(0.3f);
  filter.SetAdaptiveBeta(true, 0.15f);

  // Чередуем покой и резкие ускорения
  for (int i = 0; i < 200; ++i) {
    float ax = (i % 10 < 5) ? 0.0f : 0.8f;  // каждые 5 шагов — "разгон"
    filter.Update(ax, 0.0f, 1.0f, 10.0f, 5.0f, 3.0f, 0.01f);
  }

  float qw, qx, qy, qz;
  filter.GetQuaternion(qw, qx, qy, qz);
  float norm = std::sqrt(qw * qw + qx * qx + qy * qy + qz * qz);
  EXPECT_NEAR(norm, 1.0f, 1e-5f)
      << "Quaternion should stay normalized with adaptive beta";
}

// ═══════════════════════════════════════════════════════════════════════════
// Upside-Down IMU Mount Tests (gravity_vec support)
// ═══════════════════════════════════════════════════════════════════════════

TEST(MadgwickTest, SetVehicleFrame_InitializesQuaternion) {
  // SetVehicleFrame should initialize q_madgwick = conj(q_sv) so that
  // vehicle-frame Euler angles are immediately ~0 WITHOUT any Update calls.
  // This works for ANY mounting angle.
  for (auto& grav : std::vector<std::array<float, 3>>{
           {0.f, 0.f, 1.f},       // upside-down mount
           {0.f, 0.f, -1.f},      // normal mount (z down)
           {0.f, 1.f, 0.f},       // 90° tilt (y up)
           {0.707f, 0.f, 0.707f}  // 45° tilt
       }) {
    MadgwickFilter filter;
    float forward[3] = {1.0f, 0.0f, 0.0f};
    filter.SetVehicleFrame(grav.data(), forward, true);

    float pitch, roll, yaw;
    filter.GetEulerDeg(pitch, roll, yaw);

    EXPECT_NEAR(pitch, 0.0f, 0.1f)
        << "Pitch should be ~0 immediately after SetVehicleFrame, gravity=["
        << grav[0] << "," << grav[1] << "," << grav[2] << "]";
    EXPECT_NEAR(roll, 0.0f, 0.1f)
        << "Roll should be ~0 immediately after SetVehicleFrame, gravity=["
        << grav[0] << "," << grav[1] << "," << grav[2] << "]";
  }
}

TEST(MadgwickTest, SetVehicleFrame_PreservesConvergedYaw) {
  // Регрессия LOS-229: повторная калибровка на стоящей машине сбрасывала yaw в
  // ноль, после чего 9DOF-фильтр ~10 с догонял магнитный курс градиентным
  // спуском — в телеметрии это выглядело как фантомный поворот на 90° при
  // неподвижных гироскопе, акселерометре и магнитометре.
  MadgwickFilter filter;
  filter.SetBeta(0.5f);

  float gravity[3] = {0.0f, 0.0f, -1.0f};
  float forward[3] = {1.0f, 0.0f, 0.0f};
  filter.SetVehicleFrame(gravity, forward, true);

  // Машина стоит ровно, магнитное поле развёрнуто так, что курс ≠ 0.
  // Даём фильтру сойтись — 7500 * 2 мс = 15 с при beta=0.5, что даёт вклад
  // 0.5*15=7.5 в накопитель, с запасом больше kMinMargProgressForYawRef
  // (0.1*12=1.2), чтобы опора успела стать абсолютной до калибровки ниже.
  constexpr float kMx = 0.0f, kMy = 0.6f, kMz = -0.8f;
  for (int i = 0; i < 7500; ++i) {
    filter.UpdateWithMag(0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, kMx, kMy, kMz,
                         0.002f);
  }

  float pitch_before, roll_before, yaw_before;
  filter.GetEulerDeg(pitch_before, roll_before, yaw_before);
  ASSERT_GT(std::abs(yaw_before), 5.0f)
      << "Тест бессмысленен, если фильтр сошёлся к yaw ≈ 0";

  // Повторная калибровка на той же неподвижной машине.
  filter.SetVehicleFrame(gravity, forward, true);

  float pitch_after, roll_after, yaw_after;
  filter.GetEulerDeg(pitch_after, roll_after, yaw_after);

  EXPECT_NEAR(yaw_after, yaw_before, 0.5f)
      << "Курс должен пережить рекалибровку: машина не двигалась";
  EXPECT_NEAR(pitch_after, 0.0f, 0.1f) << "Наклон обнуляется — это штатно";
  EXPECT_NEAR(roll_after, 0.0f, 0.1f) << "Наклон обнуляется — это штатно";

  // И главное: никакого транзиента после рекалибровки.
  for (int i = 0; i < 500; ++i) {
    filter.UpdateWithMag(0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, kMx, kMy, kMz,
                         0.002f);
  }
  float pitch_settled, roll_settled, yaw_settled;
  filter.GetEulerDeg(pitch_settled, roll_settled, yaw_settled);
  EXPECT_NEAR(yaw_settled, yaw_before, 1.0f)
      << "Фильтр не должен никуда уезжать после рекалибровки";
}

TEST(MadgwickTest,
     SetVehicleFrame_FirstCalibrationDoesNotBakeInPreCalibrationYaw) {
  // Ревью PR #283 (r3630012102): если MARG уже сошёлся ДО самой первой
  // SetVehicleFrame() (use_vehicle_frame_ ещё false), GetEulerRad() внутри
  // читает yaw в СЫРОЙ СК датчика — не в СК машины. Раньше это значение
  // безусловно сохранялось как ψ, из-за чего первая калибровка могла
  // вернуть ненулевой курс вместо честного Euler≈0. Особенно заметно при
  // монтаже со смещением по курсу: стоящая машина, смотрящая вперёд,
  // с сенсором, развёрнутым на 90° в курсе, показывала бы yaw≈90° после
  // самой первой калибровки — предыдущей vehicle frame ещё не было, значит
  // сохранять нечего.
  MadgwickFilter filter;
  filter.SetBeta(0.5f);

  // Сходимся по магнитометру ДО какой-либо калибровки — чистый курс СК
  // датчика (NED), фильтр только что создан, SetVehicleFrame ещё не
  // вызывался ни разу.
  constexpr float kMx = 0.0f, kMy = 0.6f, kMz = -0.8f;
  for (int i = 0; i < 7500; ++i) {
    filter.UpdateWithMag(0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, kMx, kMy, kMz,
                         0.002f);
  }

  // Монтаж со смещением по курсу: «вперёд» машины = локальная ось Y датчика
  // (сенсор развёрнут на 90° по курсу относительно машины).
  float gravity[3] = {0.0f, 0.0f, -1.0f};
  float forward[3] = {0.0f, 1.0f, 0.0f};
  filter.SetVehicleFrame(gravity, forward, true);

  float pitch_after, roll_after, yaw_after;
  filter.GetEulerDeg(pitch_after, roll_after, yaw_after);
  EXPECT_NEAR(yaw_after, 0.0f, 0.1f)
      << "Первая калибровка не должна закреплять сырой курс СК датчика — "
         "сохранять ещё нечего (предыдущей vehicle frame не было)";
  EXPECT_NEAR(pitch_after, 0.0f, 0.1f);
  EXPECT_NEAR(roll_after, 0.0f, 0.1f);
}

TEST(MadgwickTest, SetVehicleFrame_ForcedZeroYawResetsTrustForNextCalibration) {
  // Ревью PR #283 (r3630541297): когда SetVehicleFrame() принудительно
  // обнуляет курс (preserve_yaw == false — например, самая первая
  // калибровка, хотя MARG уже сошёлся до неё), новый кватернион — НЕ
  // органически сошедшееся значение, а искусственно заданная точка старта.
  // Если флаги доверия (yaw_has_absolute_ref_/marg_correction_progress_) не
  // сбросить, они остаются от ДО-калибровочного состояния — и если СЛЕДУЮЩАЯ
  // калибровка (например, Forward сразу после Full — оба вызывают
  // SetVehicleFrame() через CalibrationManager::ProcessCompletion) случится
  // раньше, чем фильтр успеет реально сойтись под новой отправной точкой,
  // она ошибочно «сохранит» недосошедшийся курс.
  MadgwickFilter filter;
  filter.SetBeta(0.5f);

  // Сходимся по магнитометру ДО какой-либо калибровки.
  constexpr float kMx = 0.0f, kMy = 0.6f, kMz = -0.8f;
  for (int i = 0; i < 7500; ++i) {
    filter.UpdateWithMag(0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, kMx, kMy, kMz,
                         0.002f);
  }

  float gravity[3] = {0.0f, 0.0f, -1.0f};
  float forward_full[3] = {1.0f, 0.0f, 0.0f};
  // Full-калибровка: первая в жизни фильтра — принудительно обнуляет курс.
  filter.SetVehicleFrame(gravity, forward_full, true);

  // Forward-калибровка сразу следом (без единого дополнительного тика
  // UpdateWithMag — не было времени реально сойтись под новой точкой
  // старта), уточняет ось «вперёд».
  float forward_refined[3] = {0.0f, 1.0f, 0.0f};
  filter.SetVehicleFrame(gravity, forward_refined, true);

  float pitch_after, roll_after, yaw_after;
  filter.GetEulerDeg(pitch_after, roll_after, yaw_after);
  EXPECT_NEAR(yaw_after, 0.0f, 0.1f)
      << "Без сброса флагов доверия после принудительного обнуления курс "
         "ошибочно 'сохранился' бы при следующей калибровке, хотя реально "
         "сойтись не успел";
  EXPECT_NEAR(pitch_after, 0.0f, 0.1f);
  EXPECT_NEAR(roll_after, 0.0f, 0.1f);
}

TEST(MadgwickTest, UpdateWithMag_InvalidAccelResetsYawTrust) {
  // Ревью PR #283 (r3630788995): если акселерометр невалиден (мёртвый/
  // нулевой семпл), UpdateWithMag() не попадает ни в MARG-ветку (нужен
  // валидный accel), ни в явный 6DOF-фолбэк (там ТОЖЕ требуется валидный
  // accel — деградация до Update() возможна только если акселерометр
  // рабочий, а магнитометра нет) — тик сводится к чистому интегрированию
  // гироскопа, но раньше флаги доверия (yaw_has_absolute_ref_/
  // marg_correction_progress_) при этом не трогались. Если такое проседание
  // акселерометра затянется или совпадёт с калибровкой, SetVehicleFrame()
  // мог бы сохранить курс, не подкреплённый магнитометром во время
  // проседания.
  MadgwickFilter filter;
  filter.SetBeta(0.5f);

  float gravity[3] = {0.0f, 0.0f, -1.0f};
  float forward[3] = {1.0f, 0.0f, 0.0f};
  filter.SetVehicleFrame(gravity, forward, true);

  constexpr float kMx = 0.0f, kMy = 0.6f, kMz = -0.8f;
  for (int i = 0; i < 7500; ++i) {
    filter.UpdateWithMag(0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, kMx, kMy, kMz,
                         0.002f);
  }
  float pitch_before, roll_before, yaw_before;
  filter.GetEulerDeg(pitch_before, roll_before, yaw_before);
  ASSERT_GT(std::abs(yaw_before), 5.0f)
      << "Тест бессмысленен, если фильтр сошёлся к yaw ≈ 0";

  // Акселерометр отваливается (нулевой семпл), магнитометр по-прежнему
  // валиден — ни MARG-ветка, ни явный 6DOF-фолбэк не применимы.
  for (int i = 0; i < 100; ++i) {
    filter.UpdateWithMag(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, kMx, kMy, kMz,
                         0.002f);
  }

  filter.SetVehicleFrame(gravity, forward, true);
  float pitch_after, roll_after, yaw_after;
  filter.GetEulerDeg(pitch_after, roll_after, yaw_after);
  EXPECT_NEAR(yaw_after, 0.0f, 0.1f)
      << "После проседания акселерометра опора должна быть сброшена — "
         "калибровка обнуляет курс, а не сохраняет неподкреплённый";
  EXPECT_NEAR(pitch_after, 0.0f, 0.1f);
  EXPECT_NEAR(roll_after, 0.0f, 0.1f);
}

TEST(MadgwickTest, InvalidateYawTrust_ResetsProgressNotQuaternion) {
  // Ревью PR #283 (r3630102909): смена калибровки магнитометра
  // (MagCalibration::Finish()) заставляет mag_calib_->Apply() выдавать
  // другой скорректированный вектор (новый hard-iron offset) — накопленный
  // до этого прогресс сходимости yaw относился к СТАРОЙ калибровке. Без
  // сброса IMU-калибровка, завершившаяся вскоре после смены mag-калибровки,
  // могла бы закрепить курс, посчитанный по устаревшей магнитной опоре —
  // тот же фантомный переход, что и остальной LOS-229.
  MadgwickFilter filter;
  filter.SetBeta(0.5f);

  float gravity[3] = {0.0f, 0.0f, -1.0f};
  float forward[3] = {1.0f, 0.0f, 0.0f};
  filter.SetVehicleFrame(gravity, forward, true);

  constexpr float kMx = 0.0f, kMy = 0.6f, kMz = -0.8f;
  for (int i = 0; i < 7500; ++i) {
    filter.UpdateWithMag(0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, kMx, kMy, kMz,
                         0.002f);
  }
  float pitch_before, roll_before, yaw_before;
  filter.GetEulerDeg(pitch_before, roll_before, yaw_before);
  ASSERT_GT(std::abs(yaw_before), 5.0f)
      << "Тест бессмысленен, если фильтр сошёлся к yaw ≈ 0";

  float qw_before, qx_before, qy_before, qz_before;
  filter.GetQuaternion(qw_before, qx_before, qy_before, qz_before);

  // Симулируем VehicleControlUnified::FinishMagCalibration() — mag-калибровка
  // сменилась, датчик физически не двигался.
  filter.InvalidateYawTrust();

  // Сам кватернион (ориентация) не тронут — это не сброс фильтра.
  float qw_after, qx_after, qy_after, qz_after;
  filter.GetQuaternion(qw_after, qx_after, qy_after, qz_after);
  EXPECT_FLOAT_EQ(qw_after, qw_before);
  EXPECT_FLOAT_EQ(qx_after, qx_before);
  EXPECT_FLOAT_EQ(qy_after, qy_before);
  EXPECT_FLOAT_EQ(qz_after, qz_before);

  // Но опора для сохранения курса теперь не абсолютна: повторная калибровка
  // обнуляет yaw, а не сохраняет устаревший (контраст с
  // SetVehicleFrame_PreservesConvergedYaw, где БЕЗ InvalidateYawTrust()
  // курс сохранился бы).
  filter.SetVehicleFrame(gravity, forward, true);
  float pitch_after, roll_after, yaw_after;
  filter.GetEulerDeg(pitch_after, roll_after, yaw_after);
  EXPECT_NEAR(yaw_after, 0.0f, 0.1f)
      << "После InvalidateYawTrust() опора должна быть сброшена — "
         "калибровка обнуляет курс, а не сохраняет устаревший";
  EXPECT_NEAR(pitch_after, 0.0f, 0.1f);
  EXPECT_NEAR(roll_after, 0.0f, 0.1f);
}

TEST(MadgwickTest, SetVehicleFrame_RoundTripThroughDifferentMountIsConsistent) {
  // Ревью PR #283 (r3630372356): если МЕЖДУ калибровками сменился q_sv
  // (например, Forward-стадия уточнила accel_forward_vec после грубой
  // Full-стадии), «старый» vehicle-frame yaw был посчитан относительно
  // СТАРОГО монтажа — переносить его 1:1 на новый некорректно. Курс должен
  // пересчитываться через смену базиса.
  //
  // Проверяем самосогласованностью: калибровка под монтажом A → под
  // монтажом B → снова под монтажом A (без движения между вызовами) должна
  // вернуть ИСХОДНЫЙ курс. При некорректном пересчёте (или его отсутствии,
  // как раньше — прямое копирование числа) round-trip НЕ восстановит
  // исходное значение, а промежуточный курс под B совпадёт с курсом под A
  // (что и является багом: копирование, а не пересчёт).
  MadgwickFilter filter;
  filter.SetBeta(0.5f);

  float gravity[3] = {0.0f, 0.0f, -1.0f};
  float forward_a[3] = {1.0f, 0.0f, 0.0f};
  float forward_b[3] = {0.0f, 1.0f, 0.0f};  // монтаж B: 90° по курсу от A

  filter.SetVehicleFrame(gravity, forward_a, true);

  constexpr float kMx = 0.0f, kMy = 0.6f, kMz = -0.8f;
  for (int i = 0; i < 7500; ++i) {
    filter.UpdateWithMag(0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, kMx, kMy, kMz,
                         0.002f);
  }

  float pitch_a1, roll_a1, yaw_a1;
  filter.GetEulerDeg(pitch_a1, roll_a1, yaw_a1);
  ASSERT_GT(std::abs(yaw_a1), 5.0f)
      << "Тест бессмысленен, если фильтр сошёлся к yaw ≈ 0";

  // Монтаж меняется на B (например, Forward-калибровка уточнила ось).
  filter.SetVehicleFrame(gravity, forward_b, true);
  float pitch_b, roll_b, yaw_b;
  filter.GetEulerDeg(pitch_b, roll_b, yaw_b);
  EXPECT_GT(std::abs(yaw_b - yaw_a1), 5.0f)
      << "Курс под другим монтажом должен отличаться от курса под старым — "
         "если совпадает, значит число просто скопировано без пересчёта "
         "через смену базиса (баг)";

  // И обратно на A — без движения между вызовами курс должен вернуться
  // к исходному значению.
  filter.SetVehicleFrame(gravity, forward_a, true);
  float pitch_a2, roll_a2, yaw_a2;
  filter.GetEulerDeg(pitch_a2, roll_a2, yaw_a2);
  EXPECT_NEAR(yaw_a2, yaw_a1, 1.0f)
      << "Round-trip A→B→A без движения должен вернуть исходный курс — "
         "иначе курс пересчитывается через смену базиса некорректно";
  EXPECT_NEAR(pitch_a2, 0.0f, 0.1f);
  EXPECT_NEAR(roll_a2, 0.0f, 0.1f);
}

TEST(MadgwickTest, SetVehicleFrame_ResetsYawWithoutMagnetometer) {
  // Обратная сторона LOS-229: без магнитометра (6DOF) yaw — это накопленный
  // дрейф гироскопа без абсолютной опоры. Сохранять его нечего, и контракт
  // vehicle-frame («после калибровки Euler ≈ 0») должен продолжать работать.
  MadgwickFilter filter;
  filter.SetBeta(0.1f);

  float gravity[3] = {0.0f, 0.0f, -1.0f};
  float forward[3] = {1.0f, 0.0f, 0.0f};
  filter.SetVehicleFrame(gravity, forward, true);

  // Крутим машину вокруг вертикали — в 6DOF это уводит yaw и он там и остаётся.
  for (int i = 0; i < 1000; ++i) {
    filter.Update(0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 45.0f, 0.002f);
  }
  float pitch, roll, yaw_drifted;
  filter.GetEulerDeg(pitch, roll, yaw_drifted);
  ASSERT_GT(std::abs(yaw_drifted), 10.0f) << "Тест бессмысленен без дрейфа yaw";

  filter.SetVehicleFrame(gravity, forward, true);

  float pitch_after, roll_after, yaw_after;
  filter.GetEulerDeg(pitch_after, roll_after, yaw_after);
  EXPECT_NEAR(yaw_after, 0.0f, 0.1f)
      << "Без магнитометра курс не подкреплён ничем — калибровка его обнуляет";
  EXPECT_NEAR(pitch_after, 0.0f, 0.1f);
  EXPECT_NEAR(roll_after, 0.0f, 0.1f);
}

TEST(MadgwickTest, SetVehicleFrame_DoesNotPreserveStaleYawFromSingleMagSample) {
  // Ревью PR #283 (r3609421304): один-единственный mag-семпл прямо перед
  // калибровкой не должен помечать курс как «абсолютно опёртый» — MARG ещё не
  // успел скорректировать накопленный в 6DOF дрейф, градиентный спуск сходится
  // постепенно. Если бы флаг ставился на первом же семпле, SetVehicleFrame()
  // сохранил бы этот неисправленный дрейф вместо честного обнуления курса —
  // ровно тот фантомный поворот, который и был причиной LOS-229.
  MadgwickFilter filter;
  filter.SetBeta(0.1f);

  float gravity[3] = {0.0f, 0.0f, -1.0f};
  float forward[3] = {1.0f, 0.0f, 0.0f};
  filter.SetVehicleFrame(gravity, forward, true);

  // Крутим машину вокруг вертикали в 6DOF — курс уезжает и дрейфует без опоры.
  for (int i = 0; i < 1000; ++i) {
    filter.Update(0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 45.0f, 0.002f);
  }
  float pitch, roll, yaw_drifted;
  filter.GetEulerDeg(pitch, roll, yaw_drifted);
  ASSERT_GT(std::abs(yaw_drifted), 10.0f) << "Тест бессмысленен без дрейфа yaw";

  // Ровно ОДИН валидный mag-семпл прямо перед калибровкой. Поле выбрано так,
  // чтобы засеваемый курс (30°) не совпадал ни с нулём, ни с накопленным
  // 6DOF-дрейфом (≈ ±90°) — иначе тест не различал бы три исхода.
  // Для монтажа gravity=(0,0,-1), forward=(1,0,0): X_veh=(1,0,0),
  // Y_veh=(0,-1,0), поэтому ψ = atan2(-m·Y_veh, m·X_veh) = atan2(my, mx).
  constexpr float kMx = 0.6f, kMy = 0.34641f;  // atan2(0.34641, 0.6) = 30°
  filter.UpdateWithMag(0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 45.0f, kMx, kMy, -0.8f,
                       0.002f);

  filter.SetVehicleFrame(gravity, forward, true);

  float pitch_after, roll_after, yaw_after;
  filter.GetEulerDeg(pitch_after, roll_after, yaw_after);
  // LOS-221: накопленный дрейф по-прежнему НЕ переносится (в этом и был смысл
  // теста), но вместо обнуления курс теперь засевается аналитически из этого
  // самого mag-семпла — сразу в точку равновесия MARG. Одного семпла
  // достаточно: засев — замкнутая формула, а не сходимость.
  EXPECT_NEAR(yaw_after, 30.0f, 0.1f)
      << "Курс должен быть засеян из mag, а не унаследован от 6DOF-дрейфа "
         "и не обнулён";
  EXPECT_NEAR(pitch_after, 0.0f, 0.1f);
  EXPECT_NEAR(roll_after, 0.0f, 0.1f);
}

TEST(MadgwickTest, SetVehicleFrame_DoesNotPreserveYawAfterOnlyBriefMargWindow) {
  // Ревью PR #283 (r3628818049): в продакшене UpdateWithMag дёргается каждые
  // 2 мс (500 Гц control loop) независимо от частоты обновления самого
  // магнитометра (ImuHandler::FeedMadgwick подаёт кэшированный mag-семпл на
  // каждом тике). Старый счётчик «подряд идущих обновлений» открывался уже
  // за 50 таких тиков — то есть за 100 мс реального времени, хотя градиентный
  // спуск при дефолтном beta=0.1 реально сходится за ~10-11 с (см. телеметрию
  // LOS-229). Эмулируем ровно этот сценарий: 50 тиков по 2 мс (100 мс) —
  // курс всё ещё не должен считаться абсолютно опёртым.
  MadgwickFilter filter;
  filter.SetBeta(0.1f);

  float gravity[3] = {0.0f, 0.0f, -1.0f};
  float forward[3] = {1.0f, 0.0f, 0.0f};
  filter.SetVehicleFrame(gravity, forward, true);

  // Крутим машину вокруг вертикали в 6DOF — курс уезжает и дрейфует без опоры.
  for (int i = 0; i < 1000; ++i) {
    filter.Update(0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 45.0f, 0.002f);
  }
  float pitch, roll, yaw_drifted;
  filter.GetEulerDeg(pitch, roll, yaw_drifted);
  ASSERT_GT(std::abs(yaw_drifted), 10.0f) << "Тест бессмысленен без дрейфа yaw";

  // Ровно 50 MARG-тиков по 2 мс (100 мс суммарно) прямо перед калибровкой —
  // столько же, сколько раньше считалось достаточным по старому счётчику.
  // Поле даёт засеваемый курс 30° (см. соседний тест), заведомо отличимый и
  // от нуля, и от накопленного 6DOF-дрейфа.
  constexpr float kMx = 0.6f, kMy = 0.34641f;
  for (int i = 0; i < 50; ++i) {
    filter.UpdateWithMag(0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 45.0f, kMx, kMy, -0.8f,
                         0.002f);
  }

  filter.SetVehicleFrame(gravity, forward, true);

  float pitch_after, roll_after, yaw_after;
  filter.GetEulerDeg(pitch_after, roll_after, yaw_after);
  // LOS-221: за 100 мс градиентный спуск действительно не успевает сойтись —
  // но теперь это и не требуется. Курс берётся замкнутой формулой из mag, а не
  // накоплением коррекции, поэтому короткое MARG-окно больше не приводит ни к
  // сохранению недосошедшегося курса, ни к обнулению.
  EXPECT_NEAR(yaw_after, 30.0f, 0.1f)
      << "Короткое MARG-окно не мешает засеву: ψ считается аналитически";
  EXPECT_NEAR(pitch_after, 0.0f, 0.1f);
  EXPECT_NEAR(roll_after, 0.0f, 0.1f);
}

TEST(MadgwickTest, SetVehicleFrame_ShortMargWindowSeedsInsteadOfZeroing) {
  // Раньше (LOS-229, ревью PR #283 r3629255508) исход SetVehicleFrame() зависел
  // от того, успел ли накопиться marg_correction_progress_: порог
  // масштабируется по beta, и при beta=0.05 требовалось ~24 с вместо 12.
  // Недобранное окно means обнуление курса — источник рампы LOS-221, потому что
  // на загрузочной калибровке окно всегда короткое (Full — 2-4 с).
  //
  // Теперь недобранное окно уводит не в ноль, а в засев из магнитометра;
  // добранное — сохраняет сошедшийся курс. Оба пути дают ОДНО И ТО ЖЕ
  // значение, потому что засев и есть точка равновесия MARG.
  float gravity[3] = {0.0f, 0.0f, -1.0f};
  float forward[3] = {1.0f, 0.0f, 0.0f};
  constexpr float kMx = 0.0f, kMy = 0.6f, kMz = -0.8f;
  constexpr float kBetaHalf = 0.05f;  // вдвое меньше дефолтного 0.1
  // Для монтажа gravity=(0,0,-1), forward=(1,0,0): ψ = atan2(my, mx) = 90°.
  constexpr float kSeededYawDeg = 90.0f;

  // 12.5 с коррекции — заведомо меньше требуемых при beta=0.05 (~24 с),
  // значит preserve_yaw закрыт и работает засев.
  {
    MadgwickFilter filter;
    filter.SetBeta(kBetaHalf);
    filter.SetVehicleFrame(gravity, forward, true);
    for (int i = 0; i < 6250; ++i) {  // 6250 * 2 мс = 12.5 с
      filter.UpdateWithMag(0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, kMx, kMy, kMz,
                           0.002f);
    }
    filter.SetVehicleFrame(gravity, forward, true);
    float pitch_after, roll_after, yaw_after;
    filter.GetEulerDeg(pitch_after, roll_after, yaw_after);
    EXPECT_NEAR(yaw_after, kSeededYawDeg, 0.1f)
        << "Недобранное MARG-окно больше не обнуляет курс — он засевается";
  }

  // 25 с коррекции — окно добрано, курс сохраняется. Значение то же.
  {
    MadgwickFilter filter;
    filter.SetBeta(kBetaHalf);
    filter.SetVehicleFrame(gravity, forward, true);
    for (int i = 0; i < 12500; ++i) {  // 12500 * 2 мс = 25 с
      filter.UpdateWithMag(0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, kMx, kMy, kMz,
                           0.002f);
    }
    float pitch_before, roll_before, yaw_before;
    filter.GetEulerDeg(pitch_before, roll_before, yaw_before);
    // Сошедшийся градиентным спуском курс совпадает с засеваемым — прямое
    // подтверждение, что замкнутая формула попадает в точку равновесия MARG
    // (включая знак: ошибись мы в нём, значения разошлись бы на 180°).
    EXPECT_NEAR(yaw_before, kSeededYawDeg, 0.5f)
        << "Засев обязан совпадать с тем, куда MARG сходится сам";

    filter.SetVehicleFrame(gravity, forward, true);
    float pitch_after, roll_after, yaw_after;
    filter.GetEulerDeg(pitch_after, roll_after, yaw_after);
    EXPECT_NEAR(yaw_after, kSeededYawDeg, 0.1f)
        << "Добранное окно сохраняет курс — то же значение, что даёт засев";
  }
}

TEST(MadgwickTest, SetVehicleFrame_ConvergedYawSurvivesDisturbedMagSample) {
  // Ревью PR #308: смена СК может случиться и на ходу —
  // CalibrationManager::ProcessForwardDirectionRequest() (WS-команда
  // set_forward_direction) требует только gravity_valid и не проверяет
  // остановку. На ходу поле искажают токи мотора: в логах LOS-221 |m| гуляет
  // 485…770 мГс. Если бы засев имел приоритет над сохранением, один такой
  // семпл подменял бы усреднённый сошедшийся курс — скачок плюс новая рампа,
  // ровно то, что фикс должен убирать.
  float gravity[3] = {0.0f, 0.0f, -1.0f};
  float forward[3] = {1.0f, 0.0f, 0.0f};
  constexpr float kMx = 0.0f, kMy = 0.6f, kMz = -0.8f;

  MadgwickFilter filter;
  filter.SetBeta(0.1f);
  filter.SetVehicleFrame(gravity, forward, true);

  // Сходимся по чистому полю с запасом над порогом (13 с при beta=0.1).
  for (int i = 0; i < 6500; ++i) {
    filter.UpdateWithMag(0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, kMx, kMy, kMz,
                         0.002f);
  }
  float pitch, roll, yaw_converged;
  filter.GetEulerDeg(pitch, roll, yaw_converged);
  ASSERT_NEAR(yaw_converged, 90.0f, 0.5f);

  // Один искажённый семпл: поле «повёрнуто» примерно на 90° и раздуто по
  // модулю — типичная наводка от мотора. Сам по себе он сдвинет кватернион
  // лишь на один шаг градиента (beta*dt), но как ЗАСЕВ дал бы ~0°.
  filter.UpdateWithMag(0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.9f, 0.0f, -1.2f,
                       0.002f);

  // Смена направления «вперёд» посреди сессии — СК та же, курс должен
  // сохраниться.
  filter.SetVehicleFrame(gravity, forward, true);

  float pitch_after, roll_after, yaw_after;
  filter.GetEulerDeg(pitch_after, roll_after, yaw_after);
  EXPECT_NEAR(yaw_after, yaw_converged, 0.5f)
      << "Сошедшийся курс не должен подменяться искажённым мгновенным семплом";
}

TEST(MadgwickTest,
     SetVehicleFrame_MargWindowStillGatesYawWhenFieldGivesNoHeading) {
  // Засев требует ненулевой горизонтальной проекции поля в СК машины. Если
  // поле направлено вдоль вертикали машины, курса из него не извлечь
  // (kMinHorizMagSq) — и тогда решение принимает прежний гейт LOS-229 по
  // накопленной MARG-коррекции. Этот тест держит гейт под покрытием.
  float gravity[3] = {0.0f, 0.0f, -1.0f};
  float forward[3] = {1.0f, 0.0f, 0.0f};
  // Поле строго вдоль Z_veh = (0,0,-1): горизонтальная проекция ровно нулевая.
  constexpr float kMx = 0.0f, kMy = 0.0f, kMz = -0.8f;

  // Короткое окно (100 мс) — опора не набрана, курс обнуляется.
  {
    MadgwickFilter filter;
    filter.SetBeta(0.1f);
    filter.SetVehicleFrame(gravity, forward, true);
    for (int i = 0; i < 50; ++i) {  // 50 * 2 мс = 100 мс
      filter.UpdateWithMag(0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 45.0f, kMx, kMy, kMz,
                           0.002f);
    }
    float pitch, roll, yaw_before;
    filter.GetEulerDeg(pitch, roll, yaw_before);

    filter.SetVehicleFrame(gravity, forward, true);
    float pitch_after, roll_after, yaw_after;
    filter.GetEulerDeg(pitch_after, roll_after, yaw_after);
    EXPECT_NEAR(yaw_after, 0.0f, 0.1f)
        << "Вертикальное поле не даёт курса, окно не набрано — обнуление";
  }

  // Длинное окно (13 с при beta=0.1, порог 12 с) — опора набрана, курс
  // переживает рекалибровку.
  {
    MadgwickFilter filter;
    filter.SetBeta(0.1f);
    filter.SetVehicleFrame(gravity, forward, true);
    for (int i = 0; i < 6500; ++i) {  // 6500 * 2 мс = 13 с
      filter.UpdateWithMag(0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 5.0f, kMx, kMy, kMz,
                           0.002f);
    }
    float pitch, roll, yaw_before;
    filter.GetEulerDeg(pitch, roll, yaw_before);
    ASSERT_GT(std::abs(yaw_before), 5.0f)
        << "Тест бессмысленен без накопленного курса";

    filter.SetVehicleFrame(gravity, forward, true);
    float pitch_after, roll_after, yaw_after;
    filter.GetEulerDeg(pitch_after, roll_after, yaw_after);
    EXPECT_NEAR(yaw_after, yaw_before, 0.5f)
        << "Набранное MARG-окно сохраняет курс и без засева";
  }
}

TEST(MadgwickTest, SetVehicleFrame_DoesNotOverweightStaleLowBetaDwell) {
  // Ревью PR #283 (r3629340617): предыдущая версия сравнивала накопленное
  // время коррекции с порогом, пересчитанным по ТЕКУЩЕМУ beta_. Если beta
  // менялась на ходу (StabilizationManager::ApplyToFilters вызывает
  // SetBeta()), это позволяло переоценить старое, накопленное при низком
  // beta время: 12 с при beta=0.01 давали ту же «сходимость», что и 1.2 с
  // при beta=1.0, хотя реальной коррекции произошло в 100 раз меньше.
  // Теперь копится вклад effective_beta * dt_sec, а не голое время — вклад
  // низкого beta остаётся заниженным независимо от того, что beta потом
  // увеличили.
  //
  // LOS-221: проверяем это на ВЫРОЖДЕННОМ поле (строго вдоль вертикали
  // машины). При обычном поле исход определял бы засев курса из магнитометра,
  // и накопитель на результат не влиял бы вовсе; вертикальное поле курса не
  // несёт, засев отключается, и решение снова принимает гейт — ровно тот
  // сценарий, ради которого написан этот тест.
  MadgwickFilter filter;
  filter.SetBeta(0.01f);

  float gravity[3] = {0.0f, 0.0f, -1.0f};
  float forward[3] = {1.0f, 0.0f, 0.0f};
  filter.SetVehicleFrame(gravity, forward, true);

  // Крутим машину вокруг вертикали в 6DOF — курс уезжает и дрейфует без опоры.
  for (int i = 0; i < 1000; ++i) {
    filter.Update(0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 45.0f, 0.002f);
  }
  float pitch, roll, yaw_drifted;
  filter.GetEulerDeg(pitch, roll, yaw_drifted);
  ASSERT_GT(std::abs(yaw_drifted), 10.0f) << "Тест бессмысленен без дрейфа yaw";

  // 12 с MARG-коррекции при beta=0.01: вклад в накопитель — всего 0.01*12 =
  // 0.12, далеко от порога 1.2. В старой (тиковой) реализации это было бы
  // "12 секунд", то есть уже больше исходного фиксированного порога в 12 с.
  constexpr float kMx = 0.0f, kMy = 0.0f, kMz = -0.8f;  // вдоль Z_veh
  for (int i = 0; i < 6000; ++i) {                      // 6000 * 2 мс = 12 с
    filter.UpdateWithMag(0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 45.0f, kMx, kMy, kMz,
                         0.002f);
  }

  // Разгон: увеличиваем beta до 1.0. Требуемое время при таком beta —
  // 12*0.1/1.0 = 1.2 с, то есть меньше уже "прошедших" (в старой модели) 12
  // с — баг позволил бы следующему же тику ошибочно открыть опору.
  filter.SetBeta(1.0f);
  filter.UpdateWithMag(0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 45.0f, kMx, kMy, kMz,
                       0.002f);

  filter.SetVehicleFrame(gravity, forward, true);
  float pitch_after, roll_after, yaw_after;
  filter.GetEulerDeg(pitch_after, roll_after, yaw_after);
  EXPECT_NEAR(yaw_after, 0.0f, 0.1f)
      << "Смена beta не должна задним числом переоценивать накопленное при "
         "низком beta время — опора всё ещё не абсолютна, курс обнуляется";
  EXPECT_NEAR(pitch_after, 0.0f, 0.1f);
  EXPECT_NEAR(roll_after, 0.0f, 0.1f);

  // Продолжаем при beta=1.0 достаточно долго, чтобы реально накопить порог
  // (нужно ещё ~1.08 — берём с запасом 1.5 с), и убеждаемся, что опора
  // ЗАКОНОМЕРНО открывается, когда реальная сходимость действительно набрана.
  // Медленное вращение нужно, чтобы курсу было куда уйти от нуля: вертикальное
  // поле сюда его не притягивает, а без ненулевого курса сохранение нечем
  // отличить от обнуления.
  for (int i = 0; i < 750; ++i) {  // 750 * 2 мс = 1.5 с, 5 °/с → ~7.5°
    filter.UpdateWithMag(0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 5.0f, kMx, kMy, kMz,
                         0.002f);
  }
  float pitch_converged, roll_converged, yaw_converged;
  filter.GetEulerDeg(pitch_converged, roll_converged, yaw_converged);
  ASSERT_GT(std::abs(yaw_converged), 5.0f)
      << "Тест бессмысленен, если фильтр сошёлся к yaw ≈ 0";

  filter.SetVehicleFrame(gravity, forward, true);
  float pitch_final, roll_final, yaw_final;
  filter.GetEulerDeg(pitch_final, roll_final, yaw_final);
  EXPECT_NEAR(yaw_final, yaw_converged, 0.5f)
      << "После реального набора порога курс должен пережить рекалибровку";
  EXPECT_NEAR(pitch_final, 0.0f, 0.1f);
  EXPECT_NEAR(roll_final, 0.0f, 0.1f);
}

// ═══════════════════════════════════════════════════════════════════════════
// LOS-221: засев курса из магнитометра в SetVehicleFrame()
// ═══════════════════════════════════════════════════════════════════════════

namespace {

// Максимальный уход курса за прогон статических MARG-тиков, с разворотом
// перехода через ±180°.
float YawDriftDeg(MadgwickFilter& filter, int ticks, float dt_sec, float mx,
                  float my, float mz) {
  float pitch, roll, yaw_start;
  filter.GetEulerDeg(pitch, roll, yaw_start);
  float prev = yaw_start;
  float unwrapped = yaw_start;
  for (int i = 0; i < ticks; ++i) {
    filter.UpdateWithMag(0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, mx, my, mz,
                         dt_sec);
    float yaw_now;
    filter.GetEulerDeg(pitch, roll, yaw_now);
    float step = yaw_now - prev;
    if (step > 180.0f) step -= 360.0f;
    if (step < -180.0f) step += 360.0f;
    unwrapped += step;
    prev = yaw_now;
  }
  return unwrapped - yaw_start;
}

}  // namespace

TEST(MadgwickTest, SetVehicleFrame_SeedsYawFromMagnetometer) {
  // Базовый случай: курс берётся замкнутой формулой из последнего mag-семпла.
  // Для монтажа gravity=(0,0,-1), forward=(1,0,0) получается X_veh=(1,0,0),
  // Y_veh=(0,-1,0), поэтому ψ = atan2(-m·Y_veh, m·X_veh) = atan2(my, mx).
  float gravity[3] = {0.0f, 0.0f, -1.0f};
  float forward[3] = {1.0f, 0.0f, 0.0f};

  struct Case {
    float mx, my;
    float expected_yaw_deg;
  };
  const Case cases[] = {
      {0.6f, 0.0f, 0.0f},         // поле «вперёд» — курс 0
      {0.0f, 0.6f, 90.0f},        // поле «влево»
      {-0.6f, 0.0f, 180.0f},      // поле «назад»
      {0.6f, 0.34641f, 30.0f},    // произвольный курс
      {0.6f, -0.34641f, -30.0f},  // и симметричный ему
  };

  for (const auto& c : cases) {
    MadgwickFilter filter;
    filter.SetBeta(0.1f);
    filter.UpdateWithMag(0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, c.mx, c.my, -0.8f,
                         0.002f);
    filter.SetVehicleFrame(gravity, forward, true);

    float pitch, roll, yaw;
    filter.GetEulerDeg(pitch, roll, yaw);
    EXPECT_NEAR(std::abs(yaw), std::abs(c.expected_yaw_deg), 0.5f)
        << "mx=" << c.mx << " my=" << c.my;
    // Наклон обнуляется в любом случае — контракт vehicle frame.
    EXPECT_NEAR(pitch, 0.0f, 0.1f);
    EXPECT_NEAR(roll, 0.0f, 0.1f);
  }
}

TEST(MadgwickTest, SetVehicleFrame_SeededYawIsMargEquilibrium) {
  // ГЛАВНЫЙ регресс LOS-221. Засев обязан попадать ровно в точку равновесия
  // градиентного спуска: тогда после калибровки на неподвижной машине курс
  // стоит на месте. До фикса ψ обнулялся, и MARG десятками секунд вытягивал
  // курс обратно — в телеметрии это выглядело как дрейф yaw при gz≈0
  // (night_25_07: 0°→61° за 13 с; boot-лог LOS-213: 23.7°→122.8° за ~110 с).
  float gravity[3] = {0.0f, 0.0f, -1.0f};
  float forward[3] = {1.0f, 0.0f, 0.0f};
  constexpr float kMx = 0.42f, kMy = 0.43f, kMz = -0.8f;

  MadgwickFilter filter;
  filter.SetBeta(0.1f);
  filter.UpdateWithMag(0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, kMx, kMy, kMz,
                       0.002f);
  filter.SetVehicleFrame(gravity, forward, true);

  // 5000 тиков по 2 мс = 10 с полностью статического входа.
  const float drift = YawDriftDeg(filter, 5000, 0.002f, kMx, kMy, kMz);
  EXPECT_LT(std::abs(drift), 1.0f)
      << "Курс после засева должен стоять на месте, а не сходиться; уход "
      << drift << "° за 10 с";
}

TEST(MadgwickTest, SetVehicleFrame_SeedsYawOnTiltedMount) {
  // Засев должен компенсировать наклон монтажа: проекции берутся на оси
  // X_veh/Y_veh, обе ортогональные вектору гравитации, поэтому вертикальная
  // составляющая поля в курс не подмешивается. Проверяем это тем же
  // критерием равновесия — если бы компенсация наклона была неверной, MARG
  // немедленно потянул бы курс прочь от засеянного.
  constexpr float kMountPitchRad = 8.0f * static_cast<float>(M_PI) / 180.0f;
  float gravity[3] = {std::sin(kMountPitchRad), 0.0f,
                      -std::cos(kMountPitchRad)};
  float forward[3] = {std::cos(kMountPitchRad), 0.0f, std::sin(kMountPitchRad)};
  constexpr float kMx = 0.35f, kMy = 0.45f, kMz = -0.82f;

  MadgwickFilter filter;
  filter.SetBeta(0.1f);
  // Акселерометр в покое читает gravity_vec — тот же наклонный монтаж.
  filter.UpdateWithMag(gravity[0], gravity[1], gravity[2], 0.0f, 0.0f, 0.0f,
                       kMx, kMy, kMz, 0.002f);
  filter.SetVehicleFrame(gravity, forward, true);

  float pitch, roll, yaw_seeded;
  filter.GetEulerDeg(pitch, roll, yaw_seeded);
  EXPECT_NEAR(pitch, 0.0f, 0.5f) << "Наклон монтажа должен быть снят";
  EXPECT_NEAR(roll, 0.0f, 0.5f);

  float prev = yaw_seeded;
  float unwrapped = yaw_seeded;
  for (int i = 0; i < 10000; ++i) {  // 20 с
    filter.UpdateWithMag(gravity[0], gravity[1], gravity[2], 0.0f, 0.0f, 0.0f,
                         kMx, kMy, kMz, 0.002f);
    float yaw_now;
    filter.GetEulerDeg(pitch, roll, yaw_now);
    float step = yaw_now - prev;
    if (step > 180.0f) step -= 360.0f;
    if (step < -180.0f) step += 360.0f;
    unwrapped += step;
    prev = yaw_now;
  }
  EXPECT_LT(std::abs(unwrapped - yaw_seeded), 1.0f)
      << "На наклонном монтаже засев тоже обязан быть равновесием";
}

TEST(MadgwickTest, SetVehicleFrame_NoYawRampOnFirstCalibration) {
  // Сценарий boot-лога целиком: фильтр стартует с единичного кватерниона, MARG
  // успевает поработать лишь несколько секунд (Full-калибровка — 1000 семплов,
  // это ~2-4 с), затем приходит ПЕРВАЯ калибровка. Именно здесь гейт
  // yaw_has_absolute_ref_ никогда не открывался (порогу нужно 12 с при
  // beta=0.1), из-за чего ψ обнулялся на каждой загрузке.
  float gravity[3] = {0.0f, 0.0f, -1.0f};
  float forward[3] = {1.0f, 0.0f, 0.0f};
  constexpr float kMx = 0.42f, kMy = 0.43f, kMz = -0.8f;

  MadgwickFilter filter;
  filter.SetBeta(0.1f);

  // ~3 с MARG до калибровки — меньше 12-секундного порога.
  for (int i = 0; i < 1500; ++i) {
    filter.UpdateWithMag(0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, kMx, kMy, kMz,
                         0.002f);
  }

  filter.SetVehicleFrame(gravity, forward, true);
  float pitch, roll, yaw_after_calib;
  filter.GetEulerDeg(pitch, roll, yaw_after_calib);

  // Машина стоит следующие 60 с — ровно тот интервал, на котором в логе
  // наблюдалась рампа.
  const float drift = YawDriftDeg(filter, 30000, 0.002f, kMx, kMy, kMz);
  EXPECT_LT(std::abs(drift), 1.0f)
      << "После первой калибровки курс не должен «уезжать»; уход " << drift
      << "° за 60 с (в логе было 61° за 13 с)";
}

TEST(MadgwickTest, SetVehicleFrame_FallsBackToZeroWhenMagGoesStale) {
  // Устаревание магнитометра ImuHandler отражает переходом на 6DOF Update().
  // Он же инвалидирует кэш засева, поэтому калибровка без свежего поля ведёт
  // себя как раньше — курс обнуляется, а не засевается по протухшему семплу.
  float gravity[3] = {0.0f, 0.0f, -1.0f};
  float forward[3] = {1.0f, 0.0f, 0.0f};
  constexpr float kMx = 0.42f, kMy = 0.43f, kMz = -0.8f;

  MadgwickFilter filter;
  filter.SetBeta(0.1f);
  for (int i = 0; i < 1000; ++i) {
    filter.UpdateWithMag(0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, kMx, kMy, kMz,
                         0.002f);
  }
  // Магнитометр пропал — ImuHandler уходит на 6DOF.
  for (int i = 0; i < 200; ++i) {
    filter.Update(0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 10.0f, 0.002f);
  }

  filter.SetVehicleFrame(gravity, forward, true);
  float pitch, roll, yaw;
  filter.GetEulerDeg(pitch, roll, yaw);
  EXPECT_NEAR(yaw, 0.0f, 0.1f)
      << "Без свежего mag засева быть не должно — только обнуление";
}

TEST(MadgwickTest, SetVehicleFrame_FallsBackWhenMagHorizontalIsDegenerate) {
  // Порог `kMinHorizFractionSq` защищает только от истинного вырождения (поле
  // практически вдоль вертикали машины): любое реальное поле его проходит.
  float gravity[3] = {0.0f, 0.0f, -1.0f};
  float forward[3] = {1.0f, 0.0f, 0.0f};

  MadgwickFilter filter;
  filter.SetBeta(0.1f);
  // Горизонталь 1e-7 при |m| ≈ 0.8 → доля² ≈ 1.6e-14, много ниже 1e-8.
  filter.UpdateWithMag(0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1e-7f, 0.0f, -0.8f,
                       0.002f);
  filter.SetVehicleFrame(gravity, forward, true);

  float pitch, roll, yaw;
  filter.GetEulerDeg(pitch, roll, yaw);
  EXPECT_NEAR(yaw, 0.0f, 0.1f)
      << "Вырожденная горизонталь поля — засев невозможен, работает фолбэк";
}

TEST(MadgwickTest, SetVehicleFrame_SeedIsScaleInvariant) {
  // Ревью PR #308: размерность mag по контракту UpdateWithMag произвольна
  // («нужна только нормировка»). Абсолютный порог вырожденности отправлял бы
  // одно и то же поле в разных единицах по разным веткам — слабое поле
  // засевалось бы нулём и возвращало рампу. Порог относительный, поэтому
  // масштаб результата не меняет.
  float gravity[3] = {0.0f, 0.0f, -1.0f};
  float forward[3] = {1.0f, 0.0f, 0.0f};
  // Поле с большим наклонением: горизонталь ≈ 10% модуля (наклонение ~84°,
  // реалистично для высоких широт). Именно на таком поле абсолютный порог и
  // расходится с относительным — горизонталь падает ниже 1e-6 задолго до
  // того, как полный модуль упрётся в гейт mnorm2 > 1e-12 внутри
  // UpdateWithMag. Ожидаемый курс: atan2(my, mx) = 45°.
  constexpr float kMx = 0.0707f, kMy = 0.0707f, kMz = -0.995f;

  auto seeded_yaw = [&](float scale) {
    MadgwickFilter filter;
    filter.SetBeta(0.1f);
    filter.UpdateWithMag(0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, kMx * scale,
                         kMy * scale, kMz * scale, 0.002f);
    filter.SetVehicleFrame(gravity, forward, true);
    float pitch, roll, yaw;
    filter.GetEulerDeg(pitch, roll, yaw);
    return yaw;
  };

  const float ref = seeded_yaw(1.0f);
  ASSERT_NEAR(std::abs(ref), 45.0f, 0.5f) << "Опорный курс посчитан неверно";

  // scale=5e-6: полный модуль² ≈ 2.5e-11 — уверенно проходит гейт
  // UpdateWithMag, а горизонталь² ≈ 2.5e-13 уже ниже 1e-12, то есть любой
  // абсолютный порог отправил бы это поле в фолбэк и вернул рампу.
  EXPECT_NEAR(seeded_yaw(5e-6f), ref, 0.1f) << "ослабленное поле";
  EXPECT_NEAR(seeded_yaw(1e3f), ref, 0.1f) << "усиленное поле";
}

TEST(MadgwickTest, UpsideDownMount_RollNearZero) {
  // After SetVehicleFrame init + convergence, roll stays ~0
  MadgwickFilter filter;
  filter.SetBeta(0.5f);

  float gravity[3] = {0.0f, 0.0f, 1.0f};
  float forward[3] = {1.0f, 0.0f, 0.0f};
  filter.SetVehicleFrame(gravity, forward, true);

  for (int i = 0; i < 300; ++i) {
    filter.Update(0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.002f);
  }

  float pitch, roll, yaw;
  filter.GetEulerDeg(pitch, roll, yaw);

  EXPECT_NEAR(pitch, 0.0f, 2.0f);
  EXPECT_NEAR(roll, 0.0f, 2.0f);
}

TEST(MadgwickTest, NormalMount_RollNearZero) {
  // Normal mount: az = -1g. Previously a saddle point — now solved by
  // SetVehicleFrame initializing q_madgwick = conj(q_sv).
  // No perturbation or high beta needed.
  MadgwickFilter filter;
  filter.SetBeta(0.1f);

  float gravity[3] = {0.0f, 0.0f, -1.0f};
  float forward[3] = {1.0f, 0.0f, 0.0f};
  filter.SetVehicleFrame(gravity, forward, true);

  for (int i = 0; i < 300; ++i) {
    filter.Update(0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.002f);
  }

  float pitch, roll, yaw;
  filter.GetEulerDeg(pitch, roll, yaw);

  EXPECT_NEAR(pitch, 0.0f, 2.0f);
  EXPECT_NEAR(roll, 0.0f, 2.0f);
}

TEST(MadgwickTest, UpsideDownMount_DetectsPitch) {
  // Upside-down mount, tilted forward ~30° pitch
  MadgwickFilter filter;
  filter.SetBeta(0.5f);

  float gravity[3] = {0.0f, 0.0f, 1.0f};
  float forward[3] = {1.0f, 0.0f, 0.0f};
  filter.SetVehicleFrame(gravity, forward, true);

  // 30° pitch: ax = sin(30°)*1g = 0.5, az = cos(30°)*1g = 0.866
  float pitch_rad = 30.0f * M_PI / 180.0f;
  float ax = std::sin(pitch_rad);
  float az = std::cos(pitch_rad);

  for (int i = 0; i < 500; ++i) {
    filter.Update(ax, 0.0f, az, 0.0f, 0.0f, 0.0f, 0.002f);
  }

  float pitch, roll, yaw;
  filter.GetEulerDeg(pitch, roll, yaw);

  EXPECT_NEAR(std::abs(pitch), 30.0f, 5.0f)
      << "Should detect ~30° pitch with upside-down mount";
  EXPECT_NEAR(roll, 0.0f, 5.0f)
      << "Roll should remain ~0 during pure pitch tilt";
}

TEST(MadgwickTest, RealHardwareValues_PitchTracking) {
  // Reproduce the user's real hardware setup:
  //   gravity_vec = [-0.010, -0.151, -0.988]
  //   forward_vec = [0.194, 0.978, -0.075]
  // Verify: (1) pitch≈0 at rest, (2) pitch tracks when nose is lifted ~30°.
  MadgwickFilter filter;
  filter.SetBeta(0.5f);

  float grav[3] = {-0.010f, -0.151f, -0.988f};
  float fwd[3] = {0.194f, 0.978f, -0.075f};
  filter.SetVehicleFrame(grav, fwd, true);

  // Phase 1: rest — verify pitch≈0
  for (int i = 0; i < 500; ++i) {
    filter.Update(grav[0], grav[1], grav[2], 0.0f, 0.0f, 0.0f, 0.002f);
  }
  float pitch, roll, yaw;
  filter.GetEulerDeg(pitch, roll, yaw);
  EXPECT_NEAR(pitch, 0.0f, 2.0f) << "Pitch should be ~0 at rest";
  EXPECT_NEAR(roll, 0.0f, 2.0f) << "Roll should be ~0 at rest";

  // Phase 2: compute vehicle frame axes to rotate correctly.
  // Replicate SetVehicleFrame math to find Y_veh (pitch axis in sensor coords).
  auto inv_sqrt = [](float x) -> float {
    return (x > 0.f) ? 1.f / std::sqrt(x) : 0.f;
  };

  float zx = grav[0], zy = grav[1], zz = grav[2];
  float zn = inv_sqrt(zx * zx + zy * zy + zz * zz);
  zx *= zn;
  zy *= zn;
  zz *= zn;

  float fx = fwd[0], fy = fwd[1], fz = fwd[2];
  float dot_fz = fx * zx + fy * zy + fz * zz;
  fx -= dot_fz * zx;
  fy -= dot_fz * zy;
  fz -= dot_fz * zz;
  float fn = inv_sqrt(fx * fx + fy * fy + fz * fz);
  fx *= fn;
  fy *= fn;
  fz *= fn;

  // Y_veh = Z_veh × X_veh (pitch axis in sensor coords)
  float yx = zy * fz - zz * fy;
  float yy = zz * fx - zx * fz;
  float yz = zx * fy - zy * fx;

  // Phase 3: apply 30° pitch via gyro about Y_veh axis.
  // Gyro is in sensor frame (gx, gy, gz in dps).
  // Rotation rate vector = 100 dps * Y_veh_direction.
  const float rate_dps = 100.0f;
  const float gyro_gx = rate_dps * yx;
  const float gyro_gy = rate_dps * yy;
  const float gyro_gz = rate_dps * yz;
  const int pitch_samples = 150;  // 100 dps * 0.3s = 30°

  for (int i = 0; i < pitch_samples; ++i) {
    filter.Update(grav[0], grav[1], grav[2], gyro_gx, gyro_gy, gyro_gz, 0.002f);
  }

  // Phase 4: compute tilted accel (Rodrigues rotation of gravity_vec about
  // Y_veh). v' = v*cos(θ) + (k×v)*sin(θ) + k*(k·v)*(1-cos(θ))
  const float theta = 30.0f * static_cast<float>(M_PI) / 180.0f;
  const float ct = std::cos(theta), st = std::sin(theta);
  // k = Y_veh, v = grav (normalized: zx,zy,zz ... no, grav_raw)
  float vx = grav[0], vy = grav[1], vz = grav[2];
  // k × v
  float cx = yy * vz - yz * vy;
  float cy = yz * vx - yx * vz;
  float cz = yx * vy - yy * vx;
  // k · v
  float kv = yx * vx + yy * vy + yz * vz;

  float tilted_ax = vx * ct + cx * st + yx * kv * (1 - ct);
  float tilted_ay = vy * ct + cy * st + yy * kv * (1 - ct);
  float tilted_az = vz * ct + cz * st + yz * kv * (1 - ct);

  // Hold at tilted position for convergence
  for (int i = 0; i < 2000; ++i) {
    filter.Update(tilted_ax, tilted_ay, tilted_az, 0.0f, 0.0f, 0.0f, 0.002f);
  }

  filter.GetEulerDeg(pitch, roll, yaw);
  EXPECT_NEAR(std::abs(pitch), 30.0f, 8.0f) << "Pitch should track ~30° tilt";
  EXPECT_NEAR(roll, 0.0f, 10.0f)
      << "Roll should remain ~0 during pure pitch tilt";
}

TEST(MadgwickTest, SetVehicleFrame_UsesCalibratedForwardOnTiltedMount) {
  // LOS-225: gravity_vec is the raw rest accel vector, while
  // accel_forward_vec is learned from post-Apply linear acceleration. The saved
  // forward axis must still build a correct vehicle frame with the raw gravity
  // reference when the IMU is mounted with an 8° pitch.
  constexpr float kMountPitchRad = 8.0f * static_cast<float>(M_PI) / 180.0f;

  auto tilted_rest = [=]() {
    ImuData d{};
    d.ax = std::sin(kMountPitchRad);
    d.az = std::cos(kMountPitchRad);
    return d;
  };
  auto tilted_forward_accel = [=](float accel_g) {
    ImuData d{};
    d.ax = std::cos(kMountPitchRad) * accel_g + std::sin(kMountPitchRad);
    d.az = -std::sin(kMountPitchRad) * accel_g + std::cos(kMountPitchRad);
    return d;
  };

  ImuCalibration calib;
  calib.StartCalibration(CalibMode::Full, 10);
  for (int i = 0; i < 10; ++i) {
    calib.FeedSample(tilted_rest());
  }
  ASSERT_EQ(calib.GetStatus(), CalibStatus::Done);

  ASSERT_TRUE(calib.StartForwardCalibration(10));
  for (int i = 0; i < 10; ++i) {
    calib.FeedSample(tilted_forward_accel(0.3f));
  }
  ASSERT_EQ(calib.GetStatus(), CalibStatus::Done);

  const ImuCalibData& data = calib.GetData();
  EXPECT_NEAR(data.gravity_vec[0], std::sin(kMountPitchRad), 1e-5f);
  EXPECT_NEAR(std::abs(data.accel_forward_vec[2]), std::sin(kMountPitchRad),
              1e-3f)
      << "Forward axis should preserve the physical mount pitch component";

  MadgwickFilter filter;
  filter.SetVehicleFrame(data.gravity_vec, data.accel_forward_vec, true);

  for (int i = 0; i < 500; ++i) {
    filter.Update(data.gravity_vec[0], data.gravity_vec[1], data.gravity_vec[2],
                  0.0f, 0.0f, 0.0f, 0.002f);
  }

  float pitch, roll, yaw;
  filter.GetEulerDeg(pitch, roll, yaw);
  (void)yaw;
  EXPECT_NEAR(pitch, 0.0f, 1.0f);
  EXPECT_NEAR(roll, 0.0f, 1.0f);
}

TEST(MadgwickTest, SetVehicleFrame_NullGravity) {
  MadgwickFilter filter;
  float forward[3] = {1.0f, 0.0f, 0.0f};

  // Null gravity should be handled gracefully (no crash, no vehicle frame)
  filter.SetVehicleFrame(nullptr, forward, true);

  float qw, qx, qy, qz;
  filter.GetQuaternion(qw, qx, qy, qz);
  EXPECT_FLOAT_EQ(qw, 1.0f);  // identity — vehicle frame not set
}

TEST(MadgwickTest, SetVehicleFrame_ForwardParallelToGravity) {
  MadgwickFilter filter;
  float gravity[3] = {0.0f, 0.0f, 1.0f};
  float forward[3] = {0.0f, 0.0f, 1.0f};  // parallel to gravity

  // Projection of forward onto plane ⊥ gravity = 0 → should not set frame
  filter.SetVehicleFrame(gravity, forward, true);

  float qw, qx, qy, qz;
  filter.GetQuaternion(qw, qx, qy, qz);
  EXPECT_FLOAT_EQ(qw, 1.0f);  // identity — cannot build frame
}

TEST(MadgwickTest, NegativeBeta) {
  MadgwickFilter filter;

  // Negative beta is technically invalid but should not crash
  filter.SetBeta(-0.1f);

  EXPECT_FLOAT_EQ(filter.GetBeta(), -0.1f);

  // Should still produce valid quaternion (though behavior may be unexpected)
  for (int i = 0; i < 50; ++i) {
    filter.Update(0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.01f);
  }

  float qw, qx, qy, qz;
  filter.GetQuaternion(qw, qx, qy, qz);

  EXPECT_TRUE(IsQuaternionNormalized(qw, qx, qy, qz))
      << "Filter should not crash with negative beta";
}
// ═══════════════════════════════════════════════════════════════════════════
// 9DOF (UpdateWithMag) — полный калиброванный вектор магнитометра (FW-R3)
// ═══════════════════════════════════════════════════════════════════════════

// Земное поле с наклонением (dip): north + down компоненты, в долях нормы
static constexpr float kFieldN = 0.6f;
static constexpr float kFieldD = 0.8f;

TEST(MadgwickTest, UpdateWithMag_LevelSensor_YawConvergesToZero) {
  MadgwickFilter filter;
  filter.SetBeta(0.5f);

  // Сенсор горизонтален, ориентирован на север: accel = (0,0,1),
  // mag = поле как есть
  for (int i = 0; i < 2000; ++i) {
    filter.UpdateWithMag(0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, kFieldN, 0.0f,
                         kFieldD, 0.002f);
  }

  float pitch, roll, yaw;
  filter.GetEulerDeg(pitch, roll, yaw);
  EXPECT_NEAR(pitch, 0.0f, 2.0f);
  EXPECT_NEAR(roll, 0.0f, 2.0f);
  EXPECT_NEAR(yaw, 0.0f, 2.0f);
}

TEST(MadgwickTest, UpdateWithMag_TiltedSensor_YawNotDistorted) {
  // Ключевое свойство FW-R3: при наклоне сенсора (pitch 30°) полный
  // mag-вектор НЕ искажает yaw — Madgwick сам проецирует поле
  // (bx = sqrt(hx²+hy²)). Прежняя схема (px, py, dot_n) на наклонном
  // монтаже давала смешение систем координат.
  MadgwickFilter filter;
  filter.SetBeta(0.5f);

  // Сенсор повёрнут на +30° вокруг Y (pitch), yaw = 0.
  // a_s = Ry(30)^T * (0,0,1);  m_s = Ry(30)^T * (N, 0, D)
  const float c = std::cos(30.0f * 3.14159265f / 180.0f);
  const float s = std::sin(30.0f * 3.14159265f / 180.0f);
  const float ax = -s, ay = 0.0f, az = c;
  const float mx = kFieldN * c - kFieldD * s;
  const float my = 0.0f;
  const float mz = kFieldN * s + kFieldD * c;

  for (int i = 0; i < 3000; ++i) {
    filter.UpdateWithMag(ax, ay, az, 0.0f, 0.0f, 0.0f, mx, my, mz, 0.002f);
  }

  float pitch, roll, yaw;
  filter.GetEulerDeg(pitch, roll, yaw);
  EXPECT_NEAR(std::abs(pitch), 30.0f, 2.0f) << "Наклон должен отслеживаться";
  EXPECT_NEAR(roll, 0.0f, 2.0f);
  EXPECT_NEAR(yaw, 0.0f, 2.0f)
      << "Yaw не должен искажаться наклоном сенсора (FW-R3)";
}

TEST(MadgwickTest, UpdateWithMag_YawedSensor_DetectsHeading) {
  MadgwickFilter filter;
  filter.SetBeta(0.5f);

  // Сенсор горизонтален, повёрнут вокруг вертикали на 40°:
  // m_s = Rz(40)^T * (N, 0, D)
  const float c = std::cos(40.0f * 3.14159265f / 180.0f);
  const float s = std::sin(40.0f * 3.14159265f / 180.0f);
  const float mx = kFieldN * c;
  const float my = -kFieldN * s;
  const float mz = kFieldD;

  for (int i = 0; i < 3000; ++i) {
    filter.UpdateWithMag(0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, mx, my, mz,
                         0.002f);
  }

  float pitch, roll, yaw;
  filter.GetEulerDeg(pitch, roll, yaw);
  EXPECT_NEAR(std::abs(yaw), 40.0f, 3.0f)
      << "9DOF должен сходиться к курсу по магнитометру";
  EXPECT_NEAR(pitch, 0.0f, 2.0f);
  EXPECT_NEAR(roll, 0.0f, 2.0f);
}

TEST(MadgwickTest, UpdateWithMag_QuaternionStaysNormalized) {
  MadgwickFilter filter;
  filter.SetBeta(0.5f);

  for (int i = 0; i < 1000; ++i) {
    filter.UpdateWithMag(0.1f, -0.05f, 0.95f, 1.0f, -2.0f, 0.5f, 0.4f, 0.2f,
                         0.7f, 0.002f);
  }

  float qw, qx, qy, qz;
  filter.GetQuaternion(qw, qx, qy, qz);
  EXPECT_TRUE(IsQuaternionNormalized(qw, qx, qy, qz));
}
