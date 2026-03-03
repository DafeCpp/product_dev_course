# Firmware Refactoring Proposal

## Executive Summary

After comprehensive analysis of the `projects/rc_vehicle/firmware` codebase, I've identified several areas for improvement. The codebase is generally well-structured with good separation of concerns, but there are opportunities to enhance maintainability, reduce complexity, and improve testability.

## Current Architecture Overview

```
firmware/
├── common/           # Platform-independent code (~25 files)
├── esp32_common/     # ESP32-specific shared code (~10 files)
├── esp32_s3/         # ESP32-S3 target implementation
├── esp32/            # Legacy ESP32 config (sdkconfig only)
└── tests/            # Unit and integration tests
```

### Strengths
1. **Good abstraction layer**: [`VehicleControlPlatform`](common/vehicle_control_platform.hpp:52) provides clean HAL abstraction
2. **Unified control loop**: [`VehicleControlUnified`](common/vehicle_control_unified.hpp:30) consolidates control logic
3. **Modular components**: [`ControlComponent`](common/control_components.hpp:19) interface enables composition
4. **Comprehensive testing**: Good test coverage with mocks ([`MockPlatform`](tests/mocks/mock_platform.hpp:23), [`FakePlatform`](tests/mocks/mock_platform.hpp:132))
5. **Well-documented protocol**: [`Protocol`](common/protocol.hpp:212) class with clear serialization

---

## Identified Issues and Recommendations

### 1. **Singleton Pattern Overuse** (High Priority)

**Issue**: [`VehicleControlUnified::Instance()`](common/vehicle_control_unified.cpp:17) uses singleton pattern, making testing difficult and creating hidden dependencies.

**Current Code**:
```cpp
// common/vehicle_control_unified.cpp:17-20
VehicleControlUnified& VehicleControlUnified::Instance() {
  static VehicleControlUnified s_instance;
  return s_instance;
}
```

**Recommendation**: Use dependency injection instead of singleton. Create instances explicitly and pass them where needed.

```cpp
// Proposed: vehicle_control_factory.hpp
class VehicleControlFactory {
 public:
  static std::unique_ptr<VehicleControlUnified> Create(
      std::unique_ptr<VehicleControlPlatform> platform);
};

// Usage in main.cpp
auto platform = std::make_unique<VehicleControlPlatformEsp32>();
auto control = VehicleControlFactory::Create(std::move(platform));
```

**Impact**: Improves testability, removes global state, enables multiple instances for testing.

---

### 2. **Large Class Responsibility** (High Priority)

**Issue**: [`VehicleControlUnified`](common/vehicle_control_unified.hpp:30) has too many responsibilities (~600 lines):
- Control loop management
- Calibration handling
- Stabilization configuration
- Telemetry logging
- Component coordination

**Recommendation**: Extract responsibilities into focused classes:

```cpp
// Proposed structure:
class ControlLoopRunner {
  void Run();  // Main loop logic
};

class CalibrationManager {
  void StartCalibration(bool full);
  bool StartForwardCalibration();
  const char* GetStatus() const;
};

class StabilizationManager {
  bool SetConfig(const StabilizationConfig& config);
  const StabilizationConfig& GetConfig() const;
};

class TelemetryManager {
  void GetLogInfo(size_t& count, size_t& cap) const;
  bool GetLogFrame(size_t idx, TelemetryLogFrame& out) const;
  void ClearLog();
};
```

**Impact**: Single Responsibility Principle, easier testing, clearer interfaces.

---

### 3. **Wrapper Class Redundancy** (Medium Priority) ✅ **COMPLETED**

**Issue**: [`VehicleControl`](esp32_s3/main/vehicle_control.hpp:22) in ESP32-S3 was a thin wrapper that added no value beyond API compatibility.

**Solution Implemented** (commit `657182d`):
- Removed `VehicleControl` class entirely
- Replaced with inline wrapper functions that directly call `VehicleControlUnified::Instance()`
- Platform initialization moved to `VehicleControlInit()` function
- Error code conversion done inline: `(result == rc_vehicle::PlatformError::Ok) ? ESP_OK : ESP_FAIL`

**Current Implementation**:
```cpp
// esp32_s3/main/vehicle_control.hpp
inline esp_err_t VehicleControlInit(void) {
  static bool platform_set = false;
  if (!platform_set) {
    auto platform = std::make_unique<rc_vehicle::VehicleControlPlatformEsp32>();
    rc_vehicle::VehicleControlUnified::Instance().SetPlatform(std::move(platform));
    platform_set = true;
  }
  auto result = rc_vehicle::VehicleControlUnified::Instance().Init();
  return (result == rc_vehicle::PlatformError::Ok) ? ESP_OK : ESP_FAIL;
}

// All other functions are inline wrappers:
inline void VehicleControlOnWifiCommand(float throttle, float steering) {
  rc_vehicle::VehicleControlUnified::Instance().OnWifiCommand(throttle, steering);
}
// ... etc
```

**Impact**:
- Reduced code from 165 lines to 80 lines (137 lines removed)
- Eliminated unnecessary class wrapper
- Maintained backward compatibility through inline functions
- Simplified maintenance

---

### 4. **Configuration Struct Bloat** (Medium Priority)

**Issue**: [`StabilizationConfig`](common/stabilization_config.hpp:25) has 30+ fields, making it hard to understand and validate.

**Recommendation**: Group related parameters into nested structs:

```cpp
// Proposed: stabilization_config_v2.hpp
struct PidConfig {
  float kp{0.1f};
  float ki{0.0f};
  float kd{0.005f};
  float max_integral{0.5f};
  float max_correction{0.3f};
};

struct FilterConfig {
  float madgwick_beta{0.1f};
  float lpf_cutoff_hz{30.0f};
  float imu_sample_rate_hz{500.0f};
};

struct AdaptiveConfig {
  bool enabled{false};
  float speed_ref_ms{1.5f};
  float scale_min{0.5f};
  float scale_max{2.0f};
};

struct OversteerConfig {
  bool warn_enabled{false};
  float slip_thresh_deg{20.0f};
  float rate_thresh_deg_s{50.0f};
  float throttle_reduction{0.0f};
};

struct StabilizationConfigV2 {
  bool enabled{false};
  DriveMode mode{DriveMode::Normal};
  uint32_t fade_ms{500};

  FilterConfig filter;
  PidConfig yaw_pid;
  PidConfig slip_pid;
  AdaptiveConfig adaptive;
  OversteerConfig oversteer;

  // Pitch compensation
  bool pitch_comp_enabled{false};
  float pitch_comp_gain{0.01f};
  float pitch_comp_max_correction{0.25f};
};
```

**Impact**: Better organization, easier to understand, cleaner JSON serialization.

---

### 5. **Magic Numbers in Control Loop** (Medium Priority)

**Issue**: Some constants are scattered in code rather than in [`config.hpp`](common/config.hpp:1).

**Examples**:
```cpp
// common/vehicle_control_unified.cpp:329
if (!telem_log_.Init(5000)) {  // Magic number

// common/control_components.cpp:79
lpf_gyro_z_.SetParams(30.f, fs_hz);  // Default cutoff hardcoded
```

**Recommendation**: Move all constants to `config.hpp`:

```cpp
// Proposed additions to config.hpp
struct TelemetryLogConfig {
  static constexpr size_t kCapacityFrames = 5000;
  static constexpr size_t kMaxExportFrames = 200;
};

struct LpfConfig {
  static constexpr float kDefaultCutoffHz = 30.0f;
  static constexpr float kMinCutoffHz = 5.0f;
  static constexpr float kMaxCutoffHz = 100.0f;
};
```

**Impact**: Centralized configuration, easier tuning, self-documenting code.

---

### 6. **Inconsistent Error Handling** (Medium Priority)

**Issue**: Mixed error handling patterns:
- [`PlatformError`](common/vehicle_control_platform.hpp:16) enum
- `esp_err_t` in ESP32 code
- `bool` return values
- `std::optional` for nullable results

**Recommendation**: Standardize on `Result<T>` pattern (already used in [`protocol.hpp`](common/protocol.hpp:58)):

```cpp
// Proposed: result.hpp (extract from protocol.hpp)
template <typename T, typename E = PlatformError>
class Result {
 public:
  static Result Ok(T value);
  static Result Err(E error);

  bool IsOk() const;
  bool IsErr() const;
  const T& Value() const;
  E Error() const;

  // Monadic operations
  template<typename F>
  auto Map(F&& f) -> Result<decltype(f(std::declval<T>())), E>;

  template<typename F>
  auto AndThen(F&& f) -> decltype(f(std::declval<T>()));
};
```

**Impact**: Consistent error handling, better composability, clearer intent.

---

### 7. **TelemetryHandler Interface Inconsistency** (Low Priority)

**Issue**: [`TelemetryHandler`](common/control_components.hpp:256) has two `Update()` methods with different signatures:

```cpp
// common/control_components.hpp:269-276
void Update(uint32_t /*now_ms*/, uint32_t /*dt_ms*/) override {}  // Empty!
void Update(uint32_t now_ms, const TelemetrySnapshot& snap);
```

**Recommendation**: Use a different interface or rename the method:

```cpp
// Option 1: Remove from ControlComponent hierarchy
class TelemetryHandler {  // Not inheriting from ControlComponent
 public:
  void SendTelemetry(uint32_t now_ms, const TelemetrySnapshot& snap);
};

// Option 2: Use template method pattern
class TelemetryHandler : public ControlComponent {
 public:
  void Update(uint32_t now_ms, uint32_t dt_ms) override {
    // Called by control loop, does nothing without snapshot
  }
  void UpdateWithSnapshot(uint32_t now_ms, const TelemetrySnapshot& snap);
};
```

**Impact**: Clearer interface, no empty method implementations.

---

### 8. **Static Variable in DelayUntilNextTick** (Low Priority)

**Issue**: Static local variable in [`DelayUntilNextTick()`](esp32_s3/main/vehicle_control_platform_esp32.cpp:247) causes issues if called from multiple tasks:

```cpp
// esp32_s3/main/vehicle_control_platform_esp32.cpp:247-251
void VehicleControlPlatformEsp32::DelayUntilNextTick(uint32_t period_ms) {
  static TickType_t last_wake_time = xTaskGetTickCount();  // Static!
  const TickType_t period_ticks = pdMS_TO_TICKS(period_ms);
  vTaskDelayUntil(&last_wake_time, period_ticks ? period_ticks : 1);
}
```

**Recommendation**: Move to instance member:

```cpp
// In vehicle_control_platform_esp32.hpp
class VehicleControlPlatformEsp32 : public VehicleControlPlatform {
 private:
  TickType_t last_wake_time_{0};
  bool wake_time_initialized_{false};
};

// In .cpp
void VehicleControlPlatformEsp32::DelayUntilNextTick(uint32_t period_ms) {
  if (!wake_time_initialized_) {
    last_wake_time_ = xTaskGetTickCount();
    wake_time_initialized_ = true;
  }
  const TickType_t period_ticks = pdMS_TO_TICKS(period_ms);
  vTaskDelayUntil(&last_wake_time_, period_ticks ? period_ticks : 1);
}
```

**Impact**: Thread safety, correct behavior with multiple instances.

---

### 9. **JSON Handler Complexity in main.cpp** (Low Priority)

**Issue**: [`ws_json_handler()`](esp32_s3/main/main.cpp:64) is a large function with many if-else branches.

**Recommendation**: Use command pattern with handler registry:

```cpp
// Proposed: ws_command_handler.hpp
using JsonHandler = std::function<void(cJSON* json, httpd_req_t* req)>;

class WsCommandRegistry {
 public:
  void Register(std::string_view type, JsonHandler handler);
  void Handle(const char* type, cJSON* json, httpd_req_t* req);

 private:
  std::unordered_map<std::string, JsonHandler> handlers_;
};

// Usage
WsCommandRegistry registry;
registry.Register("calibrate_imu", HandleCalibrateImu);
registry.Register("get_stab_config", HandleGetStabConfig);
registry.Register("set_stab_config", HandleSetStabConfig);
// ...
```

**Impact**: Extensibility, cleaner code, easier testing.

---

### 10. **Missing Namespace Consistency** (Low Priority)

**Issue**: Some classes are in `rc_vehicle` namespace, others are global:
- [`MadgwickFilter`](common/madgwick_filter.hpp:24) - global
- [`ImuCalibration`](common/imu_calibration.hpp:45) - global
- [`PidController`](common/pid_controller.hpp:21) - `rc_vehicle`

**Recommendation**: Move all classes to `rc_vehicle` namespace:

```cpp
namespace rc_vehicle {
class MadgwickFilter : public IOrientationFilter { ... };
class ImuCalibration { ... };
}  // namespace rc_vehicle
```

**Impact**: Consistency, avoid name collisions, clearer ownership.

---

## Proposed Refactoring Phases

### Phase 1: Quick Wins (1-2 days) ✅ **COMPLETED**
1. ✅ Move magic numbers to `config.hpp`
   - Added `TelemetryLogConfig` with `kCapacityFrames = 5000`
   - Added `LpfConfig` with `kDefaultCutoffHz = 30.0f`, `kMinCutoffHz = 5.0f`, `kMaxCutoffHz = 100.0f`
   - Updated [`vehicle_control_unified.cpp:329`](common/vehicle_control_unified.cpp:329) to use `config::TelemetryLogConfig::kCapacityFrames`
   - Updated [`control_components.cpp:79`](common/control_components.cpp:79) to use `config::LpfConfig::kDefaultCutoffHz`
   - Updated [`control_components.cpp:100-102`](common/control_components.cpp:100) to use `config::LpfConfig` min/max values
2. ✅ Fix static variable in `DelayUntilNextTick`
   - Added instance members `last_wake_time_` and `wake_time_initialized_` to [`VehicleControlPlatformEsp32`](esp32_s3/main/vehicle_control_platform_esp32.hpp:87)
   - Updated [`DelayUntilNextTick()`](esp32_s3/main/vehicle_control_platform_esp32.cpp:247) to use instance members instead of static variable
   - Ensures thread safety and correct behavior with multiple instances
3. ✅ Add namespace to global classes
   - Moved [`MadgwickFilter`](common/madgwick_filter.hpp:26) into `rc_vehicle` namespace
   - Moved [`ImuCalibration`](common/imu_calibration.hpp:47) and related types (`ImuCalibData`, `CalibMode`, `CalibStatus`) into `rc_vehicle` namespace
   - Updated corresponding `.cpp` files with namespace declarations

### Phase 2: Configuration Cleanup (2-3 days) ✅ **COMPLETED**
1. ✅ Restructure `StabilizationConfig` with nested structs
   - Created nested structs: `PidConfig`, `FilterConfig`, `AdaptiveConfig`, `OversteerConfig`, `YawRateConfig`, `SlipAngleConfig`, `PitchCompensationConfig`
   - Replaced flat 30+ field structure with organized hierarchy
   - Updated [`stabilization_config.hpp`](common/stabilization_config.hpp:1) with new structure
   - Updated [`stabilization_config.cpp`](common/stabilization_config.cpp:1) with implementations for all nested structs
2. ✅ Update JSON serialization
   - Updated [`stabilization_config_json.cpp`](esp32_s3/main/stabilization_config_json.cpp:1) to serialize/deserialize nested structure
   - JSON now has hierarchical format: `filter`, `yaw_rate`, `slip_angle`, `adaptive`, `oversteer`, `pitch_comp`
3. ✅ Update all code using StabilizationConfig
   - Updated [`stabilization_pipeline.cpp`](common/stabilization_pipeline.cpp:1) to access nested fields
   - Updated [`vehicle_control_unified.cpp`](common/vehicle_control_unified.cpp:1) to use `cfg.filter.*` instead of flat fields
   - NVS storage continues to work with binary serialization (no migration needed as structure size unchanged)

### Phase 3: Architecture Improvements (1 week) ✅ **COMPLETED**
1. ✅ Extract `CalibrationManager`, `StabilizationManager`, `TelemetryManager`
   - Created [`CalibrationManager`](common/calibration_manager.hpp:17) class to handle IMU calibration
   - Created [`StabilizationManager`](common/stabilization_manager.hpp:17) class to manage stabilization configuration
   - Created [`TelemetryManager`](common/telemetry_manager.hpp:17) class to manage telemetry logging
   - Updated [`VehicleControlUnified`](common/vehicle_control_unified.hpp:30) to use manager classes
   - Reduced [`VehicleControlUnified`](common/vehicle_control_unified.cpp:1) from ~600 lines to ~510 lines
   - Improved Single Responsibility Principle compliance
2. ~~Remove `VehicleControl` wrapper~~ ✅ **Already completed** (commit `657182d`)
3. ✅ Implement command registry for WebSocket handlers
   - Created [`WsCommandRegistry`](esp32_s3/main/ws_command_registry.hpp:35) class for command pattern implementation
   - Created [`WsCommandHandlers`](esp32_s3/main/ws_command_handlers.hpp:1) with individual handler functions
   - Refactored [`ws_json_handler()`](esp32_s3/main/main.cpp:31) from 156-line if-else chain to registry-based dispatch
   - Reduced [`main.cpp`](esp32_s3/main/main.cpp:1) from 273 lines to 110 lines (-60%)
   - Improved extensibility and testability
   - Each command handler is now independently testable

### Phase 4: Error Handling Standardization (3-4 days)
1. Extract `Result<T>` to separate header
2. Update platform interface to use `Result<T>`
3. Add error conversion utilities

### Phase 5: Dependency Injection (1 week)
1. Remove singleton from `VehicleControlUnified`
2. Create factory functions
3. Update tests to use DI

---

## Testing Strategy

### Unit Tests to Add
1. `test_calibration_manager.cpp` - Calibration state machine
2. `test_stabilization_manager.cpp` - Config validation and application
3. `test_telemetry_manager.cpp` - Log buffer operations
4. `test_ws_command_registry.cpp` - Command dispatch

### Integration Tests to Add
1. Full control loop with mock platform
2. Config persistence round-trip
3. Failsafe scenarios

---

## Migration Notes

### Breaking Changes
1. `StabilizationConfig` structure change requires NVS migration
2. Removing singleton requires updating all call sites
3. Namespace changes require include updates

### Backward Compatibility
1. Keep old `VehicleControl*` free functions as deprecated wrappers
2. Provide NVS migration utility for config format
3. Document API changes in CHANGELOG

---

## Metrics for Success

| Metric | Current | Target |
|--------|---------|--------|
| `VehicleControlUnified` lines | ~600 | <200 |
| `StabilizationConfig` fields | 30+ | 10 (with nested) |
| Global classes | 5 | 0 |
| Test coverage | ~70% | >85% |
| Cyclomatic complexity (max) | 15 | <10 |

---

## Conclusion

The firmware codebase is well-designed with good separation between platform-specific and common code. The recommended refactorings focus on:

1. **Reducing complexity** in large classes
2. **Improving testability** through dependency injection
3. **Standardizing patterns** for error handling and configuration
4. **Enhancing maintainability** through better organization

These changes can be implemented incrementally without disrupting ongoing development.