# RC Vehicle Firmware Tests

This directory contains the test suite for the RC vehicle firmware project.

## Structure

```
tests/
├── CMakeLists.txt           # Build configuration for tests
├── unit/                    # Unit tests (platform-independent)
│   ├── test_protocol.cpp    # Protocol serialization/parsing tests
│   ├── test_failsafe.cpp    # Failsafe logic tests
│   ├── test_madgwick.cpp    # Madgwick filter tests
│   └── test_lpf.cpp         # Low-pass filter tests
├── integration/             # Integration tests (with mocks)
│   └── test_control_loop.cpp # Control loop integration tests
├── hil/                     # Hardware-in-the-loop tests (ESP32)
├── mocks/                   # Mock implementations
│   └── mock_platform.hpp    # Mock VehicleControlPlatform
└── fixtures/                # Test helpers and utilities
    └── test_helpers.hpp     # Common test utilities
```

## Building and Running Tests

### Prerequisites

- CMake 3.16 or higher
- C++23 compatible compiler (GCC 11+, Clang 14+, or MSVC 2022+)
- Google Test (automatically downloaded by CMake)

### Build Tests

```bash
cd tests
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

### Run Tests

```bash
cd build
ctest --output-on-failure
```

Or run specific test executables:

```bash
./unit_tests
./integration_tests
```

### Run with Coverage

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON
cmake --build build
cd build
ctest
lcov --capture --directory . --output-file coverage.info
lcov --remove coverage.info '/usr/*' --output-file coverage.info
genhtml coverage.info --output-directory coverage_html
```

## Test Categories

### Unit Tests

Platform-independent tests that verify individual components in isolation:

- **Protocol Tests** ([`test_protocol.cpp`](unit/test_protocol.cpp))
  - Frame building and parsing
  - CRC validation
  - Error handling
  - Message types (Command, Telemetry, Ping, Pong, Log)

- **Failsafe Tests** ([`test_failsafe.cpp`](unit/test_failsafe.cpp))
  - Activation/deactivation logic
  - Timeout handling
  - Recovery scenarios
  - Multiple control sources

- **Madgwick Filter Tests** ([`test_madgwick.cpp`](unit/test_madgwick.cpp))
  - Quaternion normalization
  - Gravity alignment
  - Gyroscope integration
  - Beta parameter tuning

- **Low-Pass Filter Tests** ([`test_lpf.cpp`](unit/test_lpf.cpp))
  - Frequency response
  - Step response
  - Configuration and reset
  - Stability tests

### Integration Tests

Tests that verify component interactions using mocks:

- **Control Loop Tests** ([`test_control_loop.cpp`](integration/test_control_loop.cpp))
  - RC input to PWM output flow
  - Failsafe integration
  - IMU data processing
  - Calibration save/load
  - WebSocket telemetry

### Hardware-in-the-Loop (HIL) Tests

Tests that run on actual ESP32-S3 hardware (to be implemented in Phase 4).

## Mock Platform

The [`MockPlatform`](mocks/mock_platform.hpp) class provides two implementations:

1. **MockPlatform** - Google Mock-based for strict verification
   ```cpp
   MockPlatform mock;
   EXPECT_CALL(mock, SetPwm(0.5f, 0.0f)).Times(1);
   mock.SetPwm(0.5f, 0.0f);
   ```

2. **FakePlatform** - State-based for simple scenarios
   ```cpp
   FakePlatform fake;
   fake.SetPwm(0.5f, 0.0f);
   EXPECT_FLOAT_EQ(fake.GetLastThrottle(), 0.5f);
   ```

## Test Helpers

The [`test_helpers.hpp`](fixtures/test_helpers.hpp) provides utilities:

- `FloatNear()` - Floating point comparison with tolerance
- `ExpectOptionalEq()` - Optional value assertions
- `MakeImuData()` - Create test IMU data
- `MakeRcCommand()` - Create test RC commands
- `IsQuaternionNormalized()` - Quaternion validation
- `RcVehicleTestBase` - Base test fixture class

## Writing New Tests

### Unit Test Example

```cpp
#include <gtest/gtest.h>
#include "your_component.hpp"
#include "test_helpers.hpp"

using namespace rc_vehicle::testing;

TEST(YourComponentTest, BasicFunctionality) {
  YourComponent component;

  // Arrange
  component.SetValue(42);

  // Act
  auto result = component.GetValue();

  // Assert
  EXPECT_EQ(result, 42);
}
```

### Integration Test Example

```cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "mock_platform.hpp"

using namespace rc_vehicle::testing;
using ::testing::Return;

TEST(IntegrationTest, WithMock) {
  MockPlatform mock;

  EXPECT_CALL(mock, InitPwm())
      .WillOnce(Return(PlatformError::Ok));

  EXPECT_EQ(mock.InitPwm(), PlatformError::Ok);
}
```

## Naming Conventions

- Test suite: `ComponentNameTest`
- Test case: `Method_State_ExpectedBehavior`
- Example: `TEST(ProtocolTest, BuildTelemetry_ValidData_ReturnsCorrectFrame)`

## Best Practices

1. **AAA Pattern**: Arrange, Act, Assert
2. **One assertion per test** (when possible)
3. **Descriptive test names** that explain what is being tested
4. **Clear error messages** in assertions
5. **Independent tests** that don't depend on execution order
6. **Fast tests** - unit tests should run in < 1 second total

## Coverage Goals

| Component | Target Coverage | Priority |
|-----------|----------------|----------|
| protocol.hpp | 95%+ | Critical |
| failsafe.hpp | 100% | Critical |
| madgwick_filter.hpp | 90%+ | High |
| lpf_butterworth.cpp | 90%+ | High |

## CI/CD Integration

Tests are automatically run on every push via GitHub Actions. See [`.github/workflows/firmware-tests.yml`](../../../../.github/workflows/firmware-tests.yml) for configuration.

## Troubleshooting

### Tests won't build

- Ensure you have a C++23 compatible compiler
- Check CMake version (3.16+)
- Try cleaning the build directory: `rm -rf build`

### Tests fail unexpectedly

- Check if you're running from the correct directory
- Verify all dependencies are properly linked
- Run with verbose output: `ctest --verbose`

### Coverage not working

- Ensure you built with `-DENABLE_COVERAGE=ON`
- Install lcov: `sudo apt-get install lcov` (Linux)
- Check compiler supports coverage (GCC/Clang)

## Next Steps

See [`TESTING_PLAN.md`](../TESTING_PLAN.md) for the complete testing roadmap and implementation phases.