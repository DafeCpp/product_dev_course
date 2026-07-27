#pragma once

#include <gmock/gmock.h>

#include <array>
#include <vector>

#include "control_components.hpp"  // TelemetrySnapshot, BuildTelemJson
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
 * EXPECT_CALL(mock, InitPwm()).WillOnce(Return(std::expected<void,
 * PlatformError>{})); EXPECT_CALL(mock, SetPwm(0.5f, 0.0f)).Times(1);
 * @endcode
 */
class MockPlatform : public VehicleControlPlatform {
 public:
  // ─────────────────────────────────────────────────────────────────────────
  // Инициализация
  // ─────────────────────────────────────────────────────────────────────────

  MOCK_METHOD((std::expected<void, PlatformError>), InitPwm, (), (override));
  MOCK_METHOD((std::expected<void, PlatformError>), InitRc, (), (override));
  MOCK_METHOD((std::expected<void, PlatformError>), InitImu, (), (override));
  MOCK_METHOD((std::expected<void, PlatformError>), InitFailsafe, (),
              (override));

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
  MOCK_METHOD((std::expected<void, PlatformError>), SaveCalib,
              (const ImuCalibData& data), (override));
  MOCK_METHOD((std::expected<void, PlatformError>), SaveComOffset,
              (const float offset[2]), (override));
  MOCK_METHOD(bool, LoadComOffset, (float offset[2]), (override));

  // ─────────────────────────────────────────────────────────────────────────
  // Stabilization Config
  // ─────────────────────────────────────────────────────────────────────────

  MOCK_METHOD(std::optional<StabilizationConfig>, LoadStabilizationConfig, (),
              (override));
  MOCK_METHOD(std::optional<StabilizationConfig>, LoadStabilizationConfig,
              (DriveMode mode), (override));
  MOCK_METHOD((std::expected<void, PlatformError>), SaveStabilizationConfig,
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
  MOCK_METHOD(void, PublishTelem, (const TelemetrySnapshot& snap), (override));

  // ─────────────────────────────────────────────────────────────────────────
  // Wi-Fi команды
  // ─────────────────────────────────────────────────────────────────────────

  MOCK_METHOD(std::optional<RcCommand>, TryReceiveWifiCommand, (), (override));
  MOCK_METHOD(void, SendWifiCommand, (float throttle, float steering),
              (override));

  // ─────────────────────────────────────────────────────────────────────────
  // Задачи и синхронизация
  // ─────────────────────────────────────────────────────────────────────────

  MOCK_METHOD((std::expected<void, PlatformError>), CreateTask,
              (void (*entry)(void*), void* arg), (override));
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

  std::expected<void, PlatformError> InitPwm() override {
    return std::expected<void, PlatformError>{};
  }
  std::expected<void, PlatformError> InitRc() override {
    return std::expected<void, PlatformError>{};
  }
  std::expected<void, PlatformError> InitImu() override {
    return std::expected<void, PlatformError>{};
  }
  std::expected<void, PlatformError> InitFailsafe() override {
    return std::expected<void, PlatformError>{};
  }

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
    (void)level;
    logged_messages_.emplace_back(msg);
  }

  const std::vector<std::string>& GetLoggedMessages() const {
    return logged_messages_;
  }
  void ClearLoggedMessages() { logged_messages_.clear(); }

  // ─────────────────────────────────────────────────────────────────────────
  // IMU
  // ─────────────────────────────────────────────────────────────────────────

  std::optional<ImuData> ReadImu() override { return imu_data_; }
  int GetImuLastWhoAmI() const noexcept override { return 0x68; }

  void SetImuData(const ImuData& data) { imu_data_ = data; }

  // ─────────────────────────────────────────────────────────────────────────
  // Магнетометр
  // ─────────────────────────────────────────────────────────────────────────

  std::optional<MagData> ReadMag() override {
    if (mag_read_fails_) return std::nullopt;
    return mag_data_;
  }

  void SetMagData(const MagData& data) {
    mag_data_ = data;
    mag_read_fails_ = false;
  }
  void SetMagReadShouldFail(bool fail) { mag_read_fails_ = fail; }

  bool SaveMagCalib(const MagCalibData& data) override {
    mag_calib_data_ = data;
    return true;
  }

  bool LoadMagCalib(MagCalibData& data) override {
    if (!mag_calib_data_) return false;
    data = *mag_calib_data_;
    return true;
  }

  bool EraseMagCalib() override {
    mag_calib_data_.reset();
    return true;
  }

  /** Предзаполнить «NVS» калибровкой магнитометра до Init(). */
  void SetStoredMagCalib(const MagCalibData& data) { mag_calib_data_ = data; }

  /** Лежит ли что-то в «NVS» магнитометра. */
  bool HasStoredMagCalib() const { return mag_calib_data_.has_value(); }

  // ─────────────────────────────────────────────────────────────────────────
  // Калибровка IMU
  // ─────────────────────────────────────────────────────────────────────────

  std::optional<ImuCalibData> LoadCalib() override { return calib_data_; }
  std::expected<void, PlatformError> SaveCalib(
      const ImuCalibData& data) override {
    calib_data_ = data;
    return std::expected<void, PlatformError>{};
  }
  std::expected<void, PlatformError> SaveComOffset(
      const float offset[2]) override {
    com_offset_[0] = offset[0];
    com_offset_[1] = offset[1];
    return std::expected<void, PlatformError>{};
  }
  bool LoadComOffset(float offset[2]) override {
    offset[0] = com_offset_[0];
    offset[1] = com_offset_[1];
    return com_offset_set_;
  }

  void SetCalibData(const ImuCalibData& data) { calib_data_ = data; }
  void SetComOffset(float rx, float ry) {
    com_offset_[0] = rx;
    com_offset_[1] = ry;
    com_offset_set_ = true;
  }

  // ─────────────────────────────────────────────────────────────────────────
  // Stabilization Config
  // ─────────────────────────────────────────────────────────────────────────

  std::optional<StabilizationConfig> LoadStabilizationConfig() override {
    if (active_mode_.has_value()) {
      return stab_configs_[static_cast<size_t>(*active_mode_)];
    }
    return std::nullopt;
  }

  std::optional<StabilizationConfig> LoadStabilizationConfig(
      DriveMode mode) override {
    return stab_configs_[static_cast<size_t>(mode)];
  }

  std::expected<void, PlatformError> SaveStabilizationConfig(
      const StabilizationConfig& config) override {
    stab_configs_[static_cast<size_t>(config.mode)] = config;
    active_mode_ = config.mode;
    return std::expected<void, PlatformError>{};
  }

  void SetStabilizationConfig(const StabilizationConfig& config) {
    stab_configs_[static_cast<size_t>(config.mode)] = config;
    active_mode_ = config.mode;
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

  // FW-RF8: платформа теперь получает POD-снимок, а не готовый JSON. На host
  // строим JSON здесь же (через ту же чистую BuildTelemJson, что и задача
  // телеметрии на ESP32), чтобы тесты по-прежнему проверяли содержимое кадра.
  void PublishTelem(const TelemetrySnapshot& snap) override {
    last_snap_ = snap;
    last_telem_ = BuildTelemJson(snap);
    telem_send_count_++;
  }

  void SetWebSocketClientCount(unsigned count) { ws_client_count_ = count; }
  const std::string& GetLastTelem() const { return last_telem_; }
  const TelemetrySnapshot& GetLastSnap() const { return last_snap_; }
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

  std::expected<void, PlatformError> CreateTask(void (*entry)(void*),
                                                void* arg) override {
    (void)entry;
    (void)arg;
    return std::expected<void, PlatformError>{};
  }

  void DelayUntilNextTick(uint32_t period_ms) override {
    time_ms_ += period_ms;
  }

 private:
  // Time
  uint32_t time_ms_{0};

  // Log() — const override, поэтому mutable.
  mutable std::vector<std::string> logged_messages_;

  // IMU
  std::optional<ImuData> imu_data_;
  std::optional<ImuCalibData> calib_data_;
  float com_offset_[2]{0.f, 0.f};
  bool com_offset_set_{false};

  // Магнетометр
  std::optional<MagData> mag_data_;
  bool mag_read_fails_{false};
  std::optional<MagCalibData> mag_calib_data_;

  // Stabilization (per-mode: один слот на каждый DriveMode 0..4)
  std::array<std::optional<StabilizationConfig>, 5> stab_configs_{};
  std::optional<DriveMode> active_mode_;

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
  TelemetrySnapshot last_snap_{};
  int telem_send_count_{0};

  // Wi-Fi
  std::optional<RcCommand> wifi_command_;
};

}  // namespace testing
}  // namespace rc_vehicle