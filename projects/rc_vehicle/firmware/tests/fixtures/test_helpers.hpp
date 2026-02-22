#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <cmath>
#include <optional>

#include "vehicle_control_platform.hpp"  // ImuData (via mpu6050_spi), RcCommand

namespace rc_vehicle {
namespace testing {

/**
 * @brief Custom matcher for floating point comparisons with tolerance
 */
inline ::testing::Matcher<float> FloatNear(float expected, float tolerance) {
  return ::testing::FloatNear(expected, tolerance);
}

/**
 * @brief Helper to check if optional has value and matches expected
 */
template <typename T>
void ExpectOptionalEq(const std::optional<T>& opt, const T& expected) {
  ASSERT_TRUE(opt.has_value()) << "Optional should have a value";
  EXPECT_EQ(opt.value(), expected);
}

/**
 * @brief Helper to check if optional has value and matches predicate
 */
template <typename T, typename Predicate>
void ExpectOptional(const std::optional<T>& opt, Predicate pred,
                    const char* msg = "Optional predicate failed") {
  ASSERT_TRUE(opt.has_value()) << "Optional should have a value";
  EXPECT_TRUE(pred(opt.value())) << msg;
}

/**
 * @brief Helper to check if optional is empty
 */
template <typename T>
void ExpectOptionalEmpty(const std::optional<T>& opt) {
  EXPECT_FALSE(opt.has_value()) << "Optional should be empty";
}

/**
 * @brief Helper to create ImuData with specific values (accel in g, gyro in dps)
 */
inline ImuData MakeImuData(float ax = 0.f, float ay = 0.f, float az = 1.f,
                           float gx = 0.f, float gy = 0.f, float gz = 0.f) {
  return ImuData{ax, ay, az, gx, gy, gz};
}

/**
 * @brief Helper to create RcCommand with specific values
 */
inline RcCommand MakeRcCommand(float throttle = 0.0f, float steering = 0.0f) {
  return RcCommand{throttle, steering};
}

/**
 * @brief Helper to check if two floats are approximately equal
 */
inline bool ApproxEqual(float a, float b, float epsilon = 1e-5f) {
  return std::abs(a - b) < epsilon;
}

/**
 * @brief Helper to check if quaternion is normalized
 */
inline bool IsQuaternionNormalized(float qw, float qx, float qy, float qz,
                                   float epsilon = 1e-5f) {
  float norm = std::sqrt(qw * qw + qx * qx + qy * qy + qz * qz);
  return ApproxEqual(norm, 1.0f, epsilon);
}

/**
 * @brief Test fixture base class with common setup
 */
class RcVehicleTestBase : public ::testing::Test {
 protected:
  void SetUp() override {
    // Common setup for all tests
  }

  void TearDown() override {
    // Common cleanup for all tests
  }
};

/**
 * @brief Parameterized test fixture for testing with multiple values
 */
template <typename T>
class RcVehicleParamTest : public ::testing::TestWithParam<T> {
 protected:
  void SetUp() override {
    // Common setup for parameterized tests
  }

  void TearDown() override {
    // Common cleanup for parameterized tests
  }
};

}  // namespace testing
}  // namespace rc_vehicle