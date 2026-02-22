#include <gtest/gtest.h>

#include <cmath>

#include "madgwick_filter.hpp"
#include "mpu6050_spi.hpp"
#include "test_helpers.hpp"

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

  ImuData imu =
      MakeImuData(0.f, 0.f, 1.f,   // 1g down
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