#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "control_components.hpp"    // TelemetrySnapshot
#include "diagnostics_reporter.hpp"  // DiagnosticsSnapshot
#include "imu_calibration.hpp"       // ImuCalibData
#include "imu_sensor.hpp"            // ImuData
#include "mag_sensor.hpp"            // MagData
#include "stabilization_config.hpp"
#include "vehicle_control_platform.hpp"

namespace rc_vehicle {
namespace sim {

/**
 * @brief Платформа для host-симуляции (FW-S2.1, SIL).
 *
 * Третья реализация VehicleControlPlatform рядом с esp32 и FakePlatform.
 * НЕ делает I/O сама: цикл `sim_host` ставит текущий кадр сенсоров перед Step()
 * и читает захваченные выходы после. stdin/stdout живёт в sim_host_main.cpp.
 *
 * Причина: IMU читается каждый тик (2 мс), RC — раз в 20 мс, телеметрия — по
 * интервалу; блокирующий I/O внутри ReadImu/GetRc рассинхронил бы потоки.
 *
 * Время логическое (как в FakePlatform): реальных пауз нет, время продвигает
 * вызывающий через AdvanceTimeMs().
 */
class StdioPlatform : public VehicleControlPlatform {
 public:
  StdioPlatform() = default;

  // ── Вход: ставится циклом перед каждым HostStep ──────────────────────────
  void SetImuData(const ImuData& d) { imu_data_ = d; }
  void SetMag(std::optional<MagData> m) { mag_data_ = m; }
  void SetRc(std::optional<RcCommand> rc) { rc_command_ = rc; }
  void SetWifi(std::optional<RcCommand> w) { wifi_command_ = w; }

  /** Замена реальной калибровки на identity (replay «со средней точки»). */
  void SetIdentityCalib(bool on) { identity_calib_ = on; }

  /** Режим вождения (по умолчанию Normal). */
  void SetDriveMode(DriveMode m) { drive_mode_ = m; }
  /** Лимит скорости детского режима [м/с]; >0 включает speed_limit_enabled. */
  void SetSpeedLimit(float ms) { speed_limit_ms_ = ms; }
  /** Включить стабилизацию (cfg.enabled=true) — иначе stab_weight=0 и
   *  yaw/pitch/slip/oversteer не работают. */
  void SetStabilize(bool on) { stabilize_ = on; }

  // ── Выход: читается циклом после HostStep ────────────────────────────────
  [[nodiscard]] float GetLastThrottle() const { return last_throttle_; }
  [[nodiscard]] float GetLastSteering() const { return last_steering_; }
  [[nodiscard]] bool WasNeutral() const { return last_was_neutral_; }
  [[nodiscard]] const TelemetrySnapshot& GetLastSnap() const {
    return last_snap_;
  }

  // ── Время (логическое) ───────────────────────────────────────────────────
  [[nodiscard]] uint32_t GetTimeMs() const noexcept override {
    return time_ms_;
  }
  [[nodiscard]] uint64_t GetTimeUs() const noexcept override {
    return static_cast<uint64_t>(time_ms_) * 1000;
  }
  void AdvanceTimeMs(uint32_t dt) { time_ms_ += dt; }
  void SetTimeMs(uint32_t t) { time_ms_ = t; }
  // Цикл продвигает время сам (AdvanceTimeMs), поэтому здесь no-op.
  void DelayUntilNextTick(uint32_t) override {}

  // ── Инициализация: всё успешно, чтобы Init() поднял полный тракт ──────────
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

  // ── IMU / магнитометр ────────────────────────────────────────────────────
  std::optional<ImuData> ReadImu() override { return imu_data_; }
  int GetImuLastWhoAmI() const noexcept override { return 0x68; }
  bool InitMag() override { return true; }
  std::optional<MagData> ReadMag() override { return mag_data_; }
  const char* GetMagSensorName() const noexcept override { return "sim"; }

  // ── Калибровка IMU (NVS-заглушки) ────────────────────────────────────────
  std::optional<ImuCalibData> LoadCalib() override {
    if (!identity_calib_) return std::nullopt;
    ImuCalibData id{};  // нулевой bias, gravity=(0,0,1), forward=(1,0,0)
    id.valid = true;
    return id;
  }
  std::expected<void, PlatformError> SaveCalib(const ImuCalibData&) override {
    return std::expected<void, PlatformError>{};
  }
  std::expected<void, PlatformError> SaveComOffset(const float[2]) override {
    return std::expected<void, PlatformError>{};
  }
  bool LoadComOffset(float[2]) override { return false; }

  // ── Stabilization config ─────────────────────────────────────────────────
  // Normal без лимита/стабилизации → nullopt (дефолты прошивки). Иначе строим
  // конфиг с заданным режимом + (для Kids) лимитом скорости + (с --stabilize)
  // enabled=true, чтобы yaw/pitch/slip/oversteer реально работали.
  std::optional<StabilizationConfig> LoadStabilizationConfig() override {
    return MakeConfig(drive_mode_);
  }
  std::optional<StabilizationConfig> LoadStabilizationConfig(
      DriveMode mode) override {
    return MakeConfig(mode);
  }
  std::expected<void, PlatformError> SaveStabilizationConfig(
      const StabilizationConfig&) override {
    return std::expected<void, PlatformError>{};
  }

  // ── RC / Wi-Fi ───────────────────────────────────────────────────────────
  std::optional<RcCommand> GetRc() override { return rc_command_; }
  std::optional<RcCommand> TryReceiveWifiCommand() override {
    return wifi_command_;
  }
  void SendWifiCommand(float throttle, float steering) override {
    wifi_command_ = RcCommand{throttle, steering};
  }

  // ── PWM (захват выхода) ──────────────────────────────────────────────────
  void SetPwm(float throttle, float steering) noexcept override {
    last_throttle_ = throttle;
    last_steering_ = steering;
    last_was_neutral_ = false;
  }
  void SetPwmNeutral() noexcept override {
    last_throttle_ = 0.0f;
    last_steering_ = 0.0f;
    last_was_neutral_ = true;
  }

  // ── Failsafe ─────────────────────────────────────────────────────────────
  bool FailsafeUpdate(bool rc_active, bool wifi_active) override {
    failsafe_active_ = !rc_active && !wifi_active;
    return failsafe_active_;
  }
  bool FailsafeIsActive() const noexcept override { return failsafe_active_; }

  // ── WebSocket / телеметрия ───────────────────────────────────────────────
  unsigned GetWebSocketClientCount() const noexcept override { return 0; }
  void PublishTelem(const TelemetrySnapshot& snap) override {
    last_snap_ = snap;
  }

  // ── Диагностика ──────────────────────────────────────────────────────────
  void PublishDiagnostics(const DiagnosticsSnapshot&) override {}

  // ── Задачи (поток не плодим) ─────────────────────────────────────────────
  std::expected<void, PlatformError> CreateTask(void (*)(void*),
                                                void*) override {
    return std::expected<void, PlatformError>{};
  }

  void Log(LogLevel, std::string_view) const override {}

 private:
  // Конфиг режима. Normal без лимита и без --stabilize → nullopt (дефолты
  // прошивки). Иначе строим конфиг режима (ApplyModeDefaults) + опц. лимит
  // скорости (Kids) + опц. enabled (--stabilize).
  std::optional<StabilizationConfig> MakeConfig(DriveMode mode) const {
    if (mode == DriveMode::Normal && speed_limit_ms_ <= 0.0f && !stabilize_) {
      return std::nullopt;
    }
    StabilizationConfig cfg{};
    cfg.mode = mode;
    cfg.ApplyModeDefaults();  // тюнинг выбранного режима (gains/slew/лимиты)
    if (speed_limit_ms_ > 0.0f) {
      cfg.kids_mode.speed_limit_enabled = true;
      cfg.kids_mode.max_speed_ms = speed_limit_ms_;
    }
    if (stabilize_) {
      cfg.enabled = true;  // иначе stab_weight=0 → контроллеры не работают
    }
    return cfg;
  }

  uint32_t time_ms_{0};
  std::optional<ImuData> imu_data_;
  std::optional<MagData> mag_data_;
  std::optional<RcCommand> rc_command_;
  std::optional<RcCommand> wifi_command_;
  bool identity_calib_{false};
  bool failsafe_active_{false};
  DriveMode drive_mode_{DriveMode::Normal};
  float speed_limit_ms_{0.0f};
  bool stabilize_{false};

  float last_throttle_{0.0f};
  float last_steering_{0.0f};
  bool last_was_neutral_{false};
  TelemetrySnapshot last_snap_{};
};

// ═══════════════════════════════════════════════════════════════════════════
// Протокол sim_host (построчный CSV). Свободные функции — тестируемы отдельно.
// ═══════════════════════════════════════════════════════════════════════════

/** Входной кадр (сенсоры + команды за один тик). */
struct InputFrame {
  uint32_t dt_ms{2};
  ImuData imu{};
  bool mag_present{false};
  MagData mag{};
  bool rc_present{false};
  RcCommand rc{};
  bool wifi_present{false};
  RcCommand wifi{};
};

/**
 * @brief Распарсить строку входного кадра.
 *
 * Формат (17 полей через запятую):
 *   dt_ms, ax,ay,az, gx,gy,gz,
 *   mag_present,mx,my,mz, rc_present,rc_thr,rc_str,
 * wifi_present,wifi_thr,wifi_str Единицы: accel g, gyro dps, mag мГс (как
 * ImuData/MagData).
 *
 * @return true при успешном разборе.
 */
[[nodiscard]] bool ParseInputLine(std::string_view line, InputFrame& out);

/** Заголовок колонок выходного CSV (первая строка stdout). */
[[nodiscard]] std::string OutputHeader();

/** Сформировать строку выхода: applied PWM + ключевые поля снапшота.
 *
 * @param test_active Активна ли авто-процедура теста (для SIL-сценариев
 *                    start_test; см. --start-test у sim_host).
 */
[[nodiscard]] std::string FormatOutputLine(const TelemetrySnapshot& snap,
                                           float throttle, float steering,
                                           bool neutral,
                                           bool test_active = false);

}  // namespace sim
}  // namespace rc_vehicle
