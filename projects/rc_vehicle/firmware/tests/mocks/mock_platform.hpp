#pragma once

#include <gmock/gmock.h>

#include "vehicle_control_platform.hpp"

namespace rc_vehicle {
namespace testing {

/**
 * @brief Mock implementation of VehicleControlPlatform for unit testing
 *
 * Uses Google Mock to create a testable platform implementation.
 * All methods can be configured with expectations and return values.
 *
 * Example usage:
 * @code
 * MockPlatform mock;
 * EXPECT_CALL(mock, InitPwm()).WillOnce(Return(PlatformError::Ok));
 * EXPECT_CALL(mock, SetPwm(0.5f, 0.0f)).Times(1);
 * @endcode
 */
class MockPlatform : public VehicleControlPlatform {
 public:
  // ─────────────────────────────────────────────────────────────────────────
  // Инициализация
  // ─────────────────────────────────────────────────────────────────────────

  MOCK_METHOD(PlatformError, InitPwm, (), (override));
  MOCK_METHOD(PlatformError, InitRc, (), (override));
  MOCK_METHOD(PlatformError, InitImu, (), (override));
  MOCK_METHOD(PlatformError, InitFailsafe, (), (override));

  // ─────────────────────────────────────────────────────────────────────────
  // Время
  // ─────────────────────────────────────────────────────────────────────────

  MOCK_METHOD(uint32_t, GetTimeMs, (), (const, noexcept, override));
  MOCK_METHOD(uint64_t, GetTimeUs, (), (const, noexcept, override));

  // ─────────────────────────────────────────────────────────────────────────
  // Логирование
  // ─────────────────────────────────────────────────────────────────────────

  MOCK_METHOD(void, Log, (LogLevel level, std::string_view msg),
              (const, override));

  // ─────────────────────────────────────────────────────────────────────────
  // IMU
  // ─────────────────────────────────────────────────────────────────────────

  MOCK_METHOD(std::optional<ImuData>, ReadImu, (), (override));
  MOCK_METHOD(int, GetImuLastWhoAmI, (), (const, noexcept, override));

  // ─────────────────────────────────────────────────────────────────────────
  // Калибровка IMU
  // ─────────────────────────────────────────────────────────────────────────

  MOCK_METHOD(std::optional<ImuCalibData>, LoadCalib, (), (override));
  MOCK_METHOD(bool, SaveCalib, (const ImuCalibData& data), (override));

  // ─────────────────────────────────────────────────────────────────────────
  // Stabilization Config
  // ─────────────────────────────────────────────────────────────────────────

  MOCK_METHOD(std::optional<StabilizationConfig>, LoadStabilizationConfig, (),
              (override));
  MOCK_METHOD(bool, SaveStabilizationConfig,
              (const StabilizationConfig& config), (override));

  // ─────────────────────────────────────────────────────────────────────────
  // RC Input
  // ─────────────────────────────────────────────────────────────────────────

  MOCK_METHOD(std::optional<RcCommand>, GetRc, (), (override));

  // ─────────────────────────────────────────────────────────────────────────
  // PWM Output
  // ─────────────────────────────────────────────────────────────────────────

  MOCK_METHOD(void, SetPwm, (float throttle, float steering),
              (noexcept, override));
  MOCK_METHOD(void, SetPwmNeutral, (), (noexcept, override));

  // ─────────────────────────────────────────────────────────────────────────
  // Failsafe
  // ─────────────────────────────────────────────────────────────────────────

  MOCK_METHOD(bool, FailsafeUpdate, (bool rc_active, bool wifi_active),
              (override));
  MOCK_METHOD(bool, FailsafeIsActive, (), (const, noexcept, override));

  // ─────────────────────────────────────────────────────────────────────────
  // WebSocket
  // ─────────────────────────────────────────────────────────────────────────

  MOCK_METHOD(unsigned, GetWebSocketClientCount, (),
              (const, noexcept, override));
  MOCK_METHOD(void, SendTelem, (std::string_view json), (override));

  // ─────────────────────────────────────────────────────────────────────────
  // Wi-Fi команды
  // ─────────────────────────────────────────────────────────────────────────

  MOCK_METHOD(std::optional<RcCommand>, TryReceiveWifiCommand, (), (override));
  MOCK_METHOD(void, SendWifiCommand, (float throttle, float steering),
              (override));

  // ─────────────────────────────────────────────────────────────────────────
  // Задачи и синхронизация
  // ─────────────────────────────────────────────────────────────────────────

  MOCK_METHOD(bool, CreateTask, (void (*entry)(void*), void* arg), (override));
  MOCK_METHOD(void, DelayUntilNextTick, (uint32_t period_ms), (override));
};

/**
 * @brief Fake platform implementation for simple testing scenarios
 *
 * Unlike MockPlatform, this provides actual implementations that store state.
 * Useful when you don't need to verify exact call sequences but want to
 * check the final state.
 *
 * Example usage:
 * @code
 * FakePlatform fake;
 * fake.SetPwm(0.5f, -0.3f);
 * EXPECT_FLOAT_EQ(fake.GetLastThrottle(), 0.5f);
 * EXPECT_FLOAT_EQ(fake.GetLastSteering(), -0.3f);
 * @endcode
 */
class FakePlatform : public VehicleControlPlatform {
 public:
  FakePlatform() = default;

  // ─────────────────────────────────────────────────────────────────────────
  // Инициализация
  // ─────────────────────────────────────────────────────────────────────────

  PlatformError InitPwm() override { return PlatformError::Ok; }
  PlatformError InitRc() override { return PlatformError::Ok; }
  PlatformError InitImu() override { return PlatformError::Ok; }
  PlatformError InitFailsafe() override { return PlatformError::Ok; }

  // ─────────────────────────────────────────────────────────────────────────
  // Время
  // ─────────────────────────────────────────────────────────────────────────

  uint32_t GetTimeMs() const noexcept override { return time_ms_; }
  uint64_t GetTimeUs() const noexcept override {
    return static_cast<uint64_t>(time_ms_) * 1000;
  }

  void SetTimeMs(uint32_t time_ms) { time_ms_ = time_ms; }
  void AdvanceTimeMs(uint32_t delta_ms) { time_ms_ += delta_ms; }

  // ─────────────────────────────────────────────────────────────────────────
  // Логирование
  // ─────────────────────────────────────────────────────────────────────────

  void Log(LogLevel level, std::string_view msg) const override {
    // Store for verification if needed
    (void)level;
    (void)msg;
  }

  // ─────────────────────────────────────────────────────────────────────────
  // IMU
  // ─────────────────────────────────────────────────────────────────────────

  std::optional<ImuData> ReadImu() override { return imu_data_; }
  int GetImuLastWhoAmI() const noexcept override { return 0x68; }

  void SetImuData(const ImuData& data) { imu_data_ = data; }

  // ─────────────────────────────────────────────────────────────────────────
  // Калибровка IMU
  // ─────────────────────────────────────────────────────────────────────────

  std::optional<ImuCalibData> LoadCalib() override { return calib_data_; }
  bool SaveCalib(const ImuCalibData& data) override {
    calib_data_ = data;
    return true;
  }

  void SetCalibData(const ImuCalibData& data) { calib_data_ = data; }

  // ─────────────────────────────────────────────────────────────────────────
  // Stabilization Config
  // ─────────────────────────────────────────────────────────────────────────

  std::optional<StabilizationConfig> LoadStabilizationConfig() override {
    return stab_config_;
  }

  bool SaveStabilizationConfig(const StabilizationConfig& config) override {
    stab_config_ = config;
    return true;
  }

  void SetStabilizationConfig(const StabilizationConfig& config) {
    stab_config_ = config;
  }

  // ─────────────────────────────────────────────────────────────────────────
  // RC Input
  // ─────────────────────────────────────────────────────────────────────────

  std::optional<RcCommand> GetRc() override { return rc_command_; }

  void SetRcCommand(const RcCommand& cmd) { rc_command_ = cmd; }
  void ClearRcCommand() { rc_command_ = std::nullopt; }

  // ─────────────────────────────────────────────────────────────────────────
  // PWM Output
  // ─────────────────────────────────────────────────────────────────────────

  void SetPwm(float throttle, float steering) noexcept override {
    last_throttle_ = throttle;
    last_steering_ = steering;
    pwm_set_count_++;
  }

  void SetPwmNeutral() noexcept override {
    last_throttle_ = 0.0f;
    last_steering_ = 0.0f;
    pwm_set_count_++;
  }

  float GetLastThrottle() const { return last_throttle_; }
  float GetLastSteering() const { return last_steering_; }
  int GetPwmSetCount() const { return pwm_set_count_; }

  // ─────────────────────────────────────────────────────────────────────────
  // Failsafe
  // ─────────────────────────────────────────────────────────────────────────

  bool FailsafeUpdate(bool rc_active, bool wifi_active) override {
    failsafe_active_ = !rc_active && !wifi_active;
    return failsafe_active_;
  }

  bool FailsafeIsActive() const noexcept override { return failsafe_active_; }

  void SetFailsafeActive(bool active) { failsafe_active_ = active; }

  // ─────────────────────────────────────────────────────────────────────────
  // WebSocket
  // ─────────────────────────────────────────────────────────────────────────

  unsigned GetWebSocketClientCount() const noexcept override {
    return ws_client_count_;
  }

  void SendTelem(std::string_view json) override {
    last_telem_ = std::string(json);
    telem_send_count_++;
  }

  void SetWebSocketClientCount(unsigned count) { ws_client_count_ = count; }
  const std::string& GetLastTelem() const { return last_telem_; }
  int GetTelemSendCount() const { return telem_send_count_; }

  // ─────────────────────────────────────────────────────────────────────────
  // Wi-Fi команды
  // ─────────────────────────────────────────────────────────────────────────

  std::optional<RcCommand> TryReceiveWifiCommand() override {
    return wifi_command_;
  }

  void SendWifiCommand(float throttle, float steering) override {
    wifi_command_ = RcCommand{throttle, steering};
  }

  void SetWifiCommand(const RcCommand& cmd) { wifi_command_ = cmd; }
  void ClearWifiCommand() { wifi_command_ = std::nullopt; }

  // ─────────────────────────────────────────────────────────────────────────
  // Задачи и синхронизация
  // ─────────────────────────────────────────────────────────────────────────

  bool CreateTask(void (*entry)(void*), void* arg) override {
    (void)entry;
    (void)arg;
    return true;
  }

  void DelayUntilNextTick(uint32_t period_ms) override {
    time_ms_ += period_ms;
  }

 private:
  // Time
  uint32_t time_ms_{0};

  // IMU
  std::optional<ImuData> imu_data_;
  std::optional<ImuCalibData> calib_data_;

  // Stabilization
  std::optional<StabilizationConfig> stab_config_;

  // RC Input
  std::optional<RcCommand> rc_command_;

  // PWM Output
  float last_throttle_{0.0f};
  float last_steering_{0.0f};
  int pwm_set_count_{0};

  // Failsafe
  bool failsafe_active_{false};

  // WebSocket
  unsigned ws_client_count_{0};
  std::string last_telem_;
  int telem_send_count_{0};

  // Wi-Fi
  std::optional<RcCommand> wifi_command_;
};

}  // namespace testing
}  // namespace rc_vehicle