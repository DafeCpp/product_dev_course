# План тестирования прошивки RC Vehicle

## Оглавление
1. [Обзор](#обзор)
2. [Выбор фреймворка](#выбор-фреймворка)
3. [Архитектура тестирования](#архитектура-тестирования)
4. [Unit-тесты](#unit-тесты)
5. [Интеграционные тесты](#интеграционные-тесты)
6. [Hardware-in-the-Loop (HIL) тесты](#hardware-in-the-loop-hil-тесты)
7. [Структура проекта](#структура-проекта)
8. [Стратегия моков](#стратегия-моков)
9. [План внедрения](#план-внедрения)
10. [CI/CD интеграция](#cicd-интеграция)
11. [Best Practices](#best-practices)

---

## Обзор

### Цели тестирования
- **Unit-тесты**: Проверка корректности отдельных компонентов (протокол, математика, алгоритмы)
- **Интеграционные тесты**: Проверка взаимодействия компонентов системы управления
- **HIL-тесты**: Проверка работы с реальным железом (ESP32-S3, IMU, PWM)

### Тестируемые компоненты

#### Критичные для безопасности
- [`protocol.hpp`](common/protocol.hpp) — протокол связи (CRC, парсинг, сериализация)
- [`failsafe.hpp`](common/failsafe.hpp) — система защиты от потери управления
- [`madgwick_filter.hpp`](common/madgwick_filter.hpp) — фильтр ориентации

#### Математика и алгоритмы
- [`lpf_butterworth.cpp`](common/lpf_butterworth.cpp) — фильтр низких частот
- [`imu_calibration.hpp`](common/imu_calibration.hpp) — калибровка IMU
- [`control_components.hpp`](common/control_components.hpp) — компоненты управления

#### Система управления
- [`vehicle_control_unified.hpp`](common/vehicle_control_unified.hpp) — унифицированное управление
- [`slew_rate.hpp`](common/slew_rate.hpp) — ограничение скорости изменения

---

## Выбор фреймворка

### Рекомендация: Google Test (GTest) + Google Mock (GMock)

#### Преимущества GTest/GMock
✅ **Зрелость**: Индустриальный стандарт с 2008 года
✅ **ESP-IDF интеграция**: Официальная поддержка через IDF Component Manager
✅ **C++ поддержка**: Отличная работа с C++23/26
✅ **Моки**: GMock для создания моков HAL-слоя
✅ **Параметризованные тесты**: Удобно для тестирования математики
✅ **Death tests**: Проверка assert'ов и критических ошибок
✅ **Кросс-платформенность**: Тесты можно запускать на хосте (Linux/macOS/Windows)

#### Альтернативы (не рекомендуются для данного проекта)

**Unity** (используется в ESP-IDF)
- ❌ Только C, нет поддержки C++ features
- ❌ Нет встроенных моков
- ✅ Легковесный, уже в ESP-IDF

**Catch2**
- ✅ Header-only, легко интегрировать
- ❌ Нет встроенных моков
- ❌ Менее популярен в embedded

**Doctest**
- ✅ Быстрая компиляция
- ❌ Нет моков
- ❌ Меньше функций для embedded

### Решение
**Google Test + Google Mock** — оптимальный выбор для проекта с C++23/26 и необходимостью моков.

---

## Архитектура тестирования

### Уровни тестирования

#### 1. Unit Tests (Host)
- Запускаются на хосте (Linux/macOS/Windows)
- Тестируют изолированные компоненты
- Быстрые (< 1 секунды на весь набор)
- Без зависимостей от железа

#### 2. Integration Tests (Host)
- Запускаются на хосте с моками
- Тестируют взаимодействие компонентов
- Используют mock-реализацию [`VehicleControlPlatform`](common/vehicle_control_platform.hpp)
- Средняя скорость (< 10 секунд)

#### 3. HIL Tests (Target)
- Запускаются на реальном ESP32-S3
- Тестируют работу с железом
- Медленные (минуты)
- Требуют подключенного устройства

---

## Unit-тесты

### Приоритет 1: Протокол связи

#### [`protocol.hpp`](common/protocol.hpp)

**Тесты для сериализации/десериализации:**
```cpp
// tests/unit/test_protocol.cpp
TEST(ProtocolTest, BuildTelemetryFrame) {
    TelemetryData data{
        .seq = 42,
        .status = 0x07,  // all flags set
        .ax = 1000, .ay = -500, .az = 9800,
        .gx = 100, .gy = -200, .gz = 50
    };

    std::array<uint8_t, 32> buffer;
    auto result = Protocol::BuildTelemetry(buffer, data);

    ASSERT_TRUE(IsOk(result));
    EXPECT_EQ(GetValue(result), 23);  // expected frame size

    // Verify frame structure
    EXPECT_EQ(buffer[0], FRAME_PREFIX_0);
    EXPECT_EQ(buffer[1], FRAME_PREFIX_1);
    EXPECT_EQ(buffer[2], PROTOCOL_VERSION);
    EXPECT_EQ(buffer[3], static_cast<uint8_t>(MessageType::Telemetry));
}

TEST(ProtocolTest, ParseTelemetryFrame) {
    // Build frame first
    TelemetryData original{.seq = 100, .ax = 2000};
    std::array<uint8_t, 32> buffer;
    Protocol::BuildTelemetry(buffer, original);

    // Parse it back
    auto result = Protocol::ParseTelemetry(buffer);
    ASSERT_TRUE(IsOk(result));

    auto parsed = GetValue(result);
    EXPECT_EQ(parsed.seq, original.seq);
    EXPECT_EQ(parsed.ax, original.ax);
}

TEST(ProtocolTest, DetectCorruptedCRC) {
    TelemetryData data{.seq = 1};
    std::array<uint8_t, 32> buffer;
    Protocol::BuildTelemetry(buffer, data);

    // Corrupt CRC
    buffer[21] ^= 0xFF;

    auto result = Protocol::ParseTelemetry(buffer);
    ASSERT_TRUE(IsError(result));
    EXPECT_EQ(GetError(result), ParseError::CrcMismatch);
}
```

### Приоритет 2: Фильтр Madgwick

#### [`madgwick_filter.hpp`](common/madgwick_filter.hpp)

```cpp
// tests/unit/test_madgwick.cpp
TEST(MadgwickTest, InitialQuaternionIsIdentity) {
    MadgwickFilter filter;
    float qw, qx, qy, qz;
    filter.GetQuaternion(qw, qx, qy, qz);

    EXPECT_FLOAT_EQ(qw, 1.0f);
    EXPECT_FLOAT_EQ(qx, 0.0f);
    EXPECT_FLOAT_EQ(qy, 0.0f);
    EXPECT_FLOAT_EQ(qz, 0.0f);
}

TEST(MadgwickTest, QuaternionNormalized) {
    MadgwickFilter filter;

    // Simulate some updates
    for (int i = 0; i < 100; ++i) {
        filter.Update(0.0f, 0.0f, 1.0f,  // accel (1g down)
                     0.1f, 0.0f, 0.0f,   // gyro
                     0.01f);              // dt = 10ms
    }

    float qw, qx, qy, qz;
    filter.GetQuaternion(qw, qx, qy, qz);

    float norm = std::sqrt(qw*qw + qx*qx + qy*qy + qz*qz);
    EXPECT_NEAR(norm, 1.0f, 1e-5f);
}
```

### Приоритет 3: Failsafe

#### [`failsafe.hpp`](common/failsafe.hpp)

```cpp
// tests/unit/test_failsafe.cpp
TEST(FailsafeTest, InitiallyInactive) {
    Failsafe fs(250);
    EXPECT_EQ(fs.GetState(), FailsafeState::Inactive);
    EXPECT_FALSE(fs.IsActive());
}

TEST(FailsafeTest, ActivatesAfterTimeout) {
    Failsafe fs(100);  // 100ms timeout

    uint32_t time = 0;

    // Active control
    auto state = fs.Update(time, true, false);
    EXPECT_EQ(state, FailsafeState::Inactive);

    // Lose control
    time += 50;
    state = fs.Update(time, false, false);
    EXPECT_EQ(state, FailsafeState::Inactive);  // Still within timeout

    time += 60;  // Total 110ms without control
    state = fs.Update(time, false, false);
    EXPECT_EQ(state, FailsafeState::Active);  // Should activate
    EXPECT_TRUE(fs.IsActive());
}
```

---

## Интеграционные тесты

### Mock Platform

```cpp
// tests/mocks/mock_platform.hpp
class MockPlatform : public VehicleControlPlatform {
public:
    MOCK_METHOD(PlatformError, InitPwm, (), (override));
    MOCK_METHOD(PlatformError, InitRcInput, (), (override));
    MOCK_METHOD(PlatformError, InitImu, (), (override));
    MOCK_METHOD(PlatformError, InitNvs, (), (override));

    MOCK_METHOD(void, SetPwm, (float throttle, float steering), (override));
    MOCK_METHOD(std::optional<RcCommand>, ReadRcInput, (), (override));
    MOCK_METHOD(std::optional<ImuData>, ReadImu, (), (override));

    MOCK_METHOD(uint32_t, GetTimeMs, (), (const, override));
    MOCK_METHOD(void, DelayMs, (uint32_t ms), (override));

    MOCK_METHOD(bool, LoadCalibration, (ImuCalibration& calib), (override));
    MOCK_METHOD(bool, SaveCalibration, (const ImuCalibration& calib), (override));

    MOCK_METHOD(void, SendTelemetry, (const std::string& json), (override));
    MOCK_METHOD(std::optional<RcCommand>, GetWifiCommand, (), (override));
};
```

### Тест: Control Loop с RC Input

```cpp
// tests/integration/test_control_loop.cpp
TEST(ControlLoopTest, RcInputControlsPwm) {
    auto mock = std::make_unique<MockPlatform>();
    auto* mock_ptr = mock.get();

    // Setup expectations
    EXPECT_CALL(*mock_ptr, InitPwm()).WillOnce(Return(PlatformError::Ok));
    EXPECT_CALL(*mock_ptr, InitRcInput()).WillOnce(Return(PlatformError::Ok));
    EXPECT_CALL(*mock_ptr, InitImu()).WillOnce(Return(PlatformError::Ok));
    EXPECT_CALL(*mock_ptr, InitNvs()).WillOnce(Return(PlatformError::Ok));

    // Simulate RC input
    RcCommand rc_cmd{.throttle = 0.5f, .steering = -0.3f};
    EXPECT_CALL(*mock_ptr, ReadRcInput())
        .WillRepeatedly(Return(rc_cmd));

    // Expect PWM to be set
    EXPECT_CALL(*mock_ptr, SetPwm(FloatNear(0.5f, 0.01f),
                                   FloatNear(-0.3f, 0.01f)))
        .Times(AtLeast(1));

    VehicleControlUnified& control = VehicleControlUnified::Instance();
    control.SetPlatform(std::move(mock));
    ASSERT_EQ(control.Init(), PlatformError::Ok);
}
```

---

## Hardware-in-the-Loop (HIL) тесты

### Тест: IMU Read

```cpp
// tests/hil/test_imu_hil.cpp
TEST_CASE("IMU reads valid data", "[imu][hil]") {
    auto platform = std::make_unique<VehicleControlPlatformEsp32>();
    REQUIRE(platform->InitImu() == PlatformError::Ok);

    int valid_reads = 0;
    for (int i = 0; i < 100; ++i) {
        auto data = platform->ReadImu();
        if (data.has_value()) {
            valid_reads++;

            // Check reasonable ranges
            REQUIRE(std::abs(data->ax) < 32768);

            // Gravity should be ~1g
            float g_magnitude = std::sqrt(
                data->ax * data->ax +
                data->ay * data->ay +
                data->az * data->az
            );
            REQUIRE(g_magnitude > 10000);
            REQUIRE(g_magnitude < 25000);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    REQUIRE(valid_reads > 90);  // At least 90% success rate
}
```

### HIL Test Runner (Python)

```python
# tests/hil/run_hil_tests.py
import serial
import time

class ESP32TestRunner:
    def __init__(self, port='/dev/ttyUSB0', baudrate=115200):
        self.ser = serial.Serial(port, baudrate, timeout=1)
        time.sleep(2)

    def run_tests(self):
        self.ser.write(b'run_tests\n')

        results = {'passed': 0, 'failed': 0}

        while True:
            line = self.ser.readline().decode('utf-8').strip()
            if not line:
                continue

            print(line)

            if 'TEST PASSED' in line:
                results['passed'] += 1
            elif 'TEST FAILED' in line:
                results['failed'] += 1
            elif 'ALL TESTS COMPLETE' in line:
                break

        return results
```

---

## Структура проекта

```
firmware/
├── common/                      # Platform-independent code
├── esp32_s3/                    # ESP32-S3 specific
├── tests/                       # ⭐ NEW: Test directory
│   ├── CMakeLists.txt
│   ├── unit/                   # Unit tests (host)
│   │   ├── test_protocol.cpp
│   │   ├── test_madgwick.cpp
│   │   ├── test_failsafe.cpp
│   │   └── ...
│   ├── integration/            # Integration tests (host + mocks)
│   │   ├── test_control_loop.cpp
│   │   └── ...
│   ├── hil/                    # HIL tests (ESP32)
│   │   ├── test_imu_hil.cpp
│   │   └── run_hil_tests.py
│   ├── mocks/                  # Mock implementations
│   │   └── mock_platform.hpp
│   └── fixtures/               # Test helpers
│       └── test_helpers.hpp
└── TESTING_PLAN.md
```

---

## Стратегия моков

### Mock Platform

```cpp
// tests/mocks/mock_platform.hpp
class MockPlatform : public VehicleControlPlatform {
public:
    MOCK_METHOD(PlatformError, InitPwm, (), (override));
    MOCK_METHOD(void, SetPwm, (float, float), (override));
    MOCK_METHOD(std::optional<RcCommand>, ReadRcInput, (), (override));
    // ... other methods
};
```

### Fake Platform

```cpp
// tests/mocks/fake_platform.hpp
class FakePlatform : public VehicleControlPlatform {
public:
    void SetPwm(float throttle, float steering) override {
        last_throttle_ = throttle;
        last_steering_ = steering;
    }

    float GetLastThrottle() const { return last_throttle_; }

private:
    float last_throttle_{0.0f};
    float last_steering_{0.0f};
};
```

---

## План внедрения

### Фаза 1: Инфраструктура (1-2 дня) ✅ ЗАВЕРШЕНО

- [x] Создать структуру директорий `tests/`
- [x] Настроить CMake для сборки тестов
- [x] Интегрировать Google Test
- [x] Создать базовые моки

**Результаты:**
- Создана полная структура директорий: `unit/`, `integration/`, `hil/`, `mocks/`, `fixtures/`
- Настроен [`CMakeLists.txt`](tests/CMakeLists.txt) с автоматической загрузкой Google Test v1.14.0
- Реализованы [`MockPlatform`](tests/mocks/mock_platform.hpp) и [`FakePlatform`](tests/mocks/mock_platform.hpp)
- Созданы вспомогательные утилиты в [`test_helpers.hpp`](tests/fixtures/test_helpers.hpp)
- Написаны начальные unit-тесты:
  - [`test_protocol.cpp`](tests/unit/test_protocol.cpp) - 20+ тестов для протокола
  - [`test_failsafe.cpp`](tests/unit/test_failsafe.cpp) - 15+ тестов для failsafe
  - [`test_madgwick.cpp`](tests/unit/test_madgwick.cpp) - 15+ тестов для фильтра Madgwick
  - [`test_lpf.cpp`](tests/unit/test_lpf.cpp) - 20+ тестов для LPF
- Создан [`test_control_loop.cpp`](tests/integration/test_control_loop.cpp) для интеграционных тестов
- Написан подробный [`README.md`](tests/README.md) с инструкциями по сборке и запуску

### Фаза 2: Unit-тесты (2-3 дня)

- [x] Тесты для протокола (✅ **ЗАВЕРШЕНО** - 70+ тестов)
  - [x] CRC calculation tests (consistency, different data, empty data)
  - [x] FrameBuilder tests (empty payload, max payload, buffer validation)
  - [x] FrameParser tests (header validation, payload length, frame finding)
  - [x] Telemetry tests (negative values, max values, status flags, invalid lengths)
  - [x] Command tests (zero/min/max values, clamping, sequence increment, invalid lengths)
  - [x] Log tests (empty, special chars, max length, truncation)
  - [x] Ping/Pong tests (frame sizes, invalid payloads)
  - [x] Cross-message type tests (wrong type detection)
  - [x] Robustness tests (corrupted payload, partial frames, garbage data)
  - [x] Round-trip tests (multiple iterations for telemetry and commands)
- [x] Тесты для failsafe (✅ **ЗАВЕРШЕНО** - 60+ тестов)
  - [x] Basic functionality (initial state, stays inactive with control)
  - [x] Activation tests (timeout, no initial control)
  - [x] WiFi control tests (prevents activation, either RC or WiFi)
  - [x] Recovery tests (from failsafe, with WiFi, with both sources, lose control during recovery)
  - [x] Timeout configuration (custom timeout, set timeout, default value)
  - [x] Time tracking (since last active, before first update, with active control, after reset)
  - [x] Reset tests (basic reset, during recovery, preserves timeout, multiple resets)
  - [x] Edge cases (zero timeout, time wrap-around, rapid updates)
  - [x] State transitions (full sequence, recovering with WiFi/both sources)
  - [x] Boundary conditions (exact timeout, just before timeout, very large/minimal timeout)
  - [x] Multiple activation/recovery cycles
  - [x] Intermittent control (periodic, exceeds timeout)
  - [x] Control source switching (RC/WiFi switching, simultaneous sources)
  - [x] Configuration changes (while inactive, while active)
  - [x] Stress tests (long running operation, high frequency updates)
  - [x] Real-world scenarios (RC signal dropout, WiFi connection loss, dual control with primary failure)
- [x] Тесты для Madgwick (✅ **ЗАВЕРШЕНО** - 50+ тестов)
  - [x] Initialization tests (identity quaternion, zero Euler angles)
  - [x] Quaternion normalization (stays normalized, after many updates)
  - [x] Gravity alignment (converges to gravity, detects tilt)
  - [x] Gyroscope integration (integrates rotation)
  - [x] Beta parameter (get/set, convergence speed, zero/high/negative values)
  - [x] Reset functionality (reset to identity, preserves beta)
  - [x] ImuData overload (update with struct)
  - [x] Edge cases (zero accel, very small/large dt, negative dt)
  - [x] Euler angle conversion (rad to deg, valid ranges)
  - [x] Vehicle frame transformation (valid vectors, invalid flag, null/zero forward, different orientations)
  - [x] Numerical stability (very small/large accel, high gyro rates, alternating direction)
  - [x] Multi-axis rotation (simultaneous rotation, pitch/roll/yaw independence)
  - [x] Stress tests (long-running stability, repeated reset and update)
- [x] Тесты для LPF (✅ **ЗАВЕРШЕНО** - 70+ тестов)
  - [x] Initialization tests (not configured, configuration sets parameters)
  - [x] Basic filtering tests (initial output, step returns filtered value, converges to constant)
  - [x] Frequency response tests (attenuates high frequency, passes low frequency)
  - [x] Reset tests (clears state, allows new filtering)
  - [x] Parameter change tests (reconfigure changes response)
  - [x] Typical use cases (gyro filtering, step response)
  - [x] Edge cases (zero input, negative input, alternating input, very low/high cutoff)
  - [x] Stability tests (large input, rapid changes)
  - [x] Invalid parameter tests (zero/negative cutoff, zero/negative sample rate, cutoff above Nyquist)
  - [x] Unconfigured filter behavior (passthrough, output updates)
  - [x] Reconfiguration tests (invalid to valid, valid to invalid)
  - [x] Boundary conditions (cutoff just below Nyquist, very small cutoff, very high/low sample rate)
  - [x] Numerical precision tests (very small/large values, mixed scale inputs)
  - [x] Long-running stability tests (10000+ iterations, multiple reset cycles)
  - [x] Phase lag tests (introduces phase delay)
  - [x] Coefficient validation (different cutoffs produce different behavior, same cutoff produces same behavior)
  - [x] Real-world scenarios (gyro Z axis filtering, sudden maneuver response, vibration rejection)

### Фаза 3: Интеграционные тесты (2-3 дня)

- [ ] Control loop с моками
- [ ] Failsafe integration
- [ ] Calibration flow

### Фаза 4: HIL-тесты (2-3 дня)

- [ ] IMU, PWM, RC input
- [ ] Python test runner
- [ ] Full system integration

### Фаза 5: CI/CD (1 день)

- [ ] GitHub Actions workflow
- [ ] Coverage reporting

---

## CI/CD интеграция

### GitHub Actions

```yaml
# .github/workflows/firmware-tests.yml
name: Firmware Tests

on:
  push:
    paths:
      - 'projects/rc_vehicle/firmware/**'

jobs:
  unit-tests:
    runs-on: ubuntu-latest

    steps:
    - uses: actions/checkout@v3

    - name: Install dependencies
      run: |
        sudo apt-get update
        sudo apt-get install -y cmake g++ lcov

    - name: Build tests
      working-directory: projects/rc_vehicle/firmware/tests
      run: |
        cmake -B build -DCMAKE_BUILD_TYPE=Debug
        cmake --build build

    - name: Run tests
      working-directory: projects/rc_vehicle/firmware/tests/build
      run: ctest --output-on-failure
```

### CMake конфигурация

```cmake
# tests/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
project(rc_vehicle_tests CXX)

set(CMAKE_CXX_STANDARD 23)

# Google Test
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

enable_testing()
include(GoogleTest)

# Unit tests
add_executable(unit_tests
    ../common/protocol.cpp
    ../common/madgwick_filter.cpp
    ../common/failsafe.cpp
    unit/test_protocol.cpp
    unit/test_madgwick.cpp
    unit/test_failsafe.cpp
)

target_link_libraries(unit_tests gtest gtest_main gmock)
gtest_discover_tests(unit_tests)
```

---

## Best Practices

### 1. Naming Conventions

```cpp
// Test suite: ComponentNameTest
TEST(ProtocolTest, BuildTelemetryFrame) { ... }

// Test name: Method_State_Expected
TEST(FailsafeTest, Update_NoControl_Activates) { ... }
```

### 2. AAA Pattern

```cpp
TEST(ComponentTest, TestName) {
    // Arrange
    MockPlatform mock;

    // Act
    auto result = component.DoSomething();

    // Assert
    EXPECT_EQ(result, expected);
}
```

### 3. Descriptive Assertions

```cpp
// ❌ Bad
EXPECT_TRUE(value > 0);

// ✅ Good
EXPECT_GT(value, 0) << "Value should be positive, got: " << value;
```

---

## Метрики качества

### Coverage Goals

| Component | Target | Priority |
|-----------|--------|----------|
| protocol.hpp | 95%+ | Critical |
| failsafe.hpp | 100% | Critical |
| madgwick_filter.hpp | 90%+ | High |
| lpf_butterworth.cpp | 90%+ | High |

### Test Execution Time

- Unit tests: < 1 second
- Integration tests: < 10 seconds
- HIL tests: < 5 minutes

---

## Заключение

Этот план обеспечивает:

✅ **Полное покрытие**: Unit, Integration, HIL
✅ **Автоматизация**: CI/CD интеграция
✅ **Качество**: Coverage goals и quality gates
✅ **Масштабируемость**: Легко добавлять тесты

**Следующие шаги:**
1. Создать структуру `tests/`
2. Настроить CMake и Google Test
3. Начать с unit-тестов для протокола
4. Постепенно добавлять остальные тесты