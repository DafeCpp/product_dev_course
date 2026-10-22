#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "config.hpp"                // таймауты RC/failsafe (LOS-368)
#include "control_components.hpp"    // TelemetrySnapshot
#include "diagnostics_reporter.hpp"  // DiagnosticsSnapshot
#include "failsafe.hpp"              // Failsafe (LOS-362)
#include "imu_calibration.hpp"       // ImuCalibData
#include "imu_sensor.hpp"            // ImuData
#include "mag_sensor.hpp"            // MagData
#include "rc_link_monitor.hpp"       // RcLinkEvent, FormatRcLinkEvent
#include "rc_pulse_decoder.hpp"      // RcPulseDecoder (LOS-216)
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

  /**
   * LOS-216: режим сырых RC-импульсов. Вместо готовой RcCommand каналы
   * идут через настоящий RcPulseDecoder (как ISR на ESP32): каждые
   * kRcFramePeriodUs выдаётся импульс заданной ширины [мкс], 0 — канал
   * молчит. GetRc()/GetRcStats() отвечают по декодерам. Режим включается
   * первым вызовом и действует до конца прогона.
   */
  void SetRcPulses(uint32_t throttle_us, uint32_t steering_us) {
    if (!rc_pulse_mode_) rc_next_pulse_us_ = GetTimeUs();
    rc_pulse_mode_ = true;
    rc_thr_pulse_us_ = throttle_us;
    rc_str_pulse_us_ = steering_us;
  }

  /** Печатать Log() и события RC-связи в stderr (sim_host --log). */
  void SetLogToStderr(bool on) { log_to_stderr_ = on; }
  void SetWifi(std::optional<RcCommand> w) { wifi_command_ = w; }

  /** Замена реальной калибровки на identity (replay «со средней точки»). */
  void SetIdentityCalib(bool on) {
    identity_calib_ = on;
    if (on) inverted_z_calib_ = false;
  }
  /** Калибровка перевёрнутого монтажа: Z датчика направлен вниз,
   *  X совпадает с направлением движения. */
  void SetInvertedZCalib(bool on) {
    inverted_z_calib_ = on;
    if (on) identity_calib_ = false;
  }
  /** Сохранённая калибровка магнитометра для real-data replay. */
  void SetMagCalib(std::optional<MagCalibData> calib) {
    mag_calib_ = std::move(calib);
  }

  /** Режим вождения (по умолчанию Normal). */
  void SetDriveMode(DriveMode m) { drive_mode_ = m; }
  /** Лимит скорости детского режима [м/с]; >0 включает speed_limit_enabled. */
  void SetSpeedLimit(float ms) { speed_limit_ms_ = ms; }
  /** Включить стабилизацию (cfg.enabled=true) — иначе stab_weight=0 и
   *  yaw/pitch/slip/oversteer не работают. */
  void SetStabilize(bool on) { stabilize_ = on; }
  /** Выключить accel limiter детского режима (эталон для SIL, LOS-362). */
  void SetKidsAccelLimit(bool on) { kids_accel_limit_ = on; }
  /** Выключить anti-spin детского режима вместе с дублирующим его
   *  oversteer-срезом газа (эталон для SIL, LOS-370). */
  void SetKidsAntiSpin(bool on) { kids_anti_spin_ = on; }
  /** Явный oversteer-конфиг из provenance replay-эпизода. */
  void SetOversteerConfig(std::optional<OversteerConfig> cfg) {
    oversteer_config_ = cfg;
  }
  /** Включить компенсацию тангажа (pitch_comp.enabled), LOS-367. */
  void SetPitchComp(bool on) { pitch_comp_enabled_ = on; }
  /** Включить компенсацию крена с коэффициентом gain (roll_comp), LOS-386. */
  void SetRollCompGain(float gain) { roll_comp_gain_ = gain; }
  /** Целевой угол скольжения Drift-режима [deg] (slip_angle.target_deg),
   *  LOS-367. */
  void SetSlipTargetDeg(float deg) { slip_target_deg_ = deg; }
  /** Возрастной пресет Kids Mode (ApplyPreset), LOS-367. */
  void SetKidsPreset(KidsPreset preset) { kids_preset_ = preset; }
  /** Madgwick β (filter.madgwick_beta) с выключенным adaptive β, как в
   *  пункте 7.7 протокола: иначе на манёвре β обнуляется (LOS-384). */
  void SetMadgwickBeta(float beta) { madgwick_beta_ = beta; }
  /** Частота среза LPF gyro Z [Гц] (filter.lpf_cutoff_hz), LOS-384. */
  void SetLpfCutoffHz(float hz) { lpf_cutoff_hz_ = hz; }
  /** Сохранённый в NVS offset IMU от центра масс [м] (LOS-384). */
  void SetComOffset(float rx, float ry) {
    com_offset_ = std::array<float, 2>{rx, ry};
  }

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
  void AdvanceTimeMs(uint32_t dt) {
    time_ms_ += dt;
    if (rc_pulse_mode_) EmitRcPulses();
  }
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
  bool LoadMagCalib(MagCalibData& data) override {
    if (!mag_calib_) return false;
    data = *mag_calib_;
    return true;
  }

  // ── Калибровка IMU (NVS-заглушки) ────────────────────────────────────────
  std::optional<ImuCalibData> LoadCalib() override {
    if (!identity_calib_ && !inverted_z_calib_) return std::nullopt;
    ImuCalibData calib{};  // нулевой bias, forward=(1,0,0)
    if (inverted_z_calib_) {
      calib.gravity_vec[2] = -1.0f;
      calib.gravity_valid = true;
      calib.forward_valid = true;
    }
    calib.valid = true;
    return calib;
  }
  std::expected<void, PlatformError> SaveCalib(const ImuCalibData&) override {
    return std::expected<void, PlatformError>{};
  }
  std::expected<void, PlatformError> SaveComOffset(
      const float offset[2]) override {
    com_offset_ = std::array<float, 2>{offset[0], offset[1]};
    return std::expected<void, PlatformError>{};
  }
  bool LoadComOffset(float offset[2]) override {
    if (!com_offset_) return false;
    offset[0] = (*com_offset_)[0];
    offset[1] = (*com_offset_)[1];
    return true;
  }

  // ── Stabilization config ─────────────────────────────────────────────────
  // Normal без флагов → nullopt (дефолты прошивки). Иначе — конфиг режима
  // с переопределениями из флагов sim_host (см. MakeConfig).
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
  std::optional<RcCommand> GetRc() override {
    if (!rc_pulse_mode_) return rc_command_;
    const uint64_t now_us = GetTimeUs();
    const auto thr = rc_thr_decoder_.Read(now_us);
    const auto str = rc_str_decoder_.Read(now_us);
    if (!thr || !str) return std::nullopt;
    return RcCommand{.throttle = *thr, .steering = *str};
  }
  std::optional<RcInputStats> GetRcStats() override {
    if (!rc_pulse_mode_) return std::nullopt;
    const uint64_t now_us = GetTimeUs();
    return RcInputStats{.throttle = rc_thr_decoder_.Status(now_us),
                        .steering = rc_str_decoder_.Status(now_us)};
  }
  void PublishRcLinkEvent(const RcLinkEvent& evt) override {
    Log(evt.kind == RcLinkEvent::Kind::Lost ? LogLevel::Warning
                                            : LogLevel::Info,
        FormatRcLinkEvent(evt));
  }
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
  // Тот же Failsafe с тем же таймаутом, что на ESP32 (FAILSAFE_TIMEOUT_MS в
  // esp32_s3/main/config.hpp): без grace-периода SIL показывал бы нейтраль
  // раньше, чем машина (LOS-362).
  bool FailsafeUpdate(bool rc_active, bool wifi_active) override {
    return failsafe_.Update(time_ms_, rc_active, wifi_active) ==
           FailsafeState::Active;
  }
  bool FailsafeIsActive() const noexcept override {
    return failsafe_.IsActive();
  }

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

  void Log(LogLevel level, std::string_view msg) const override {
    if (!log_to_stderr_) return;
    const char* tag = level == LogLevel::Error     ? "E"
                      : level == LogLevel::Warning ? "W"
                                                   : "I";
    std::cerr << tag << " (" << time_ms_ << ") " << msg << '\n';
  }

 private:
  // Конфиг режима. Normal без флагов → nullopt (дефолты прошивки). Иначе
  // ApplyModeDefaults() + переопределения: oversteer, Kids-пресет, лимит
  // скорости, pitch_comp, slip target, accel limiter, β и cutoff фильтров.
  // --stabilize,
  // --pitch-comp и --slip-target-deg включают enabled (иначе stab_weight=0).
  std::optional<StabilizationConfig> MakeConfig(DriveMode mode) const {
    if (mode == DriveMode::Normal && speed_limit_ms_ <= 0.0f && !stabilize_ &&
        !oversteer_config_ && !pitch_comp_enabled_ && !roll_comp_gain_ &&
        !slip_target_deg_ && kids_preset_ == KidsPreset::Custom &&
        !madgwick_beta_ && !lpf_cutoff_hz_) {
      return std::nullopt;
    }
    StabilizationConfig cfg{};
    cfg.mode = mode;
    cfg.ApplyModeDefaults();  // тюнинг выбранного режима (gains/slew/лимиты)
    if (oversteer_config_) cfg.oversteer = *oversteer_config_;
    if (kids_preset_ != KidsPreset::Custom)
      cfg.kids_mode.ApplyPreset(kids_preset_);
    if (speed_limit_ms_ > 0.0f) {
      cfg.kids_mode.speed_limit_enabled = true;
      cfg.kids_mode.max_speed_ms = speed_limit_ms_;
    }
    if (stabilize_ || pitch_comp_enabled_ || roll_comp_gain_ ||
        slip_target_deg_) {
      cfg.enabled = true;  // иначе stab_weight=0 → контроллеры не работают
    }
    if (pitch_comp_enabled_) cfg.pitch_comp.enabled = true;
    if (roll_comp_gain_) {
      cfg.roll_comp.enabled = true;
      cfg.roll_comp.gain = *roll_comp_gain_;
    }
    if (slip_target_deg_) cfg.slip_angle.target_deg = *slip_target_deg_;
    if (!kids_accel_limit_) cfg.kids_mode.accel_limit_enabled = false;
    if (madgwick_beta_) {
      cfg.filter.madgwick_beta = *madgwick_beta_;
      cfg.filter.adaptive_beta_enabled = false;
    }
    if (lpf_cutoff_hz_) cfg.filter.lpf_cutoff_hz = *lpf_cutoff_hz_;
    if (!kids_anti_spin_) {
      cfg.kids_mode.anti_spin_enabled = false;
      // В Kids oversteer-гард настроен из anti_spin_* и режет газ тем же
      // сигналом (KidsModeStrategy::ApplyDefaults) — без него эталон не чист.
      cfg.oversteer.throttle_reduction = 0.0f;
    }
    return cfg;
  }

  // Только импульсы, у которых спад уже наступил (fall <= now): как на
  // ESP32, где ISR видит фронт не раньше, чем он случился.
  void EmitRcPulses() {
    const uint64_t now_us = GetTimeUs();
    const uint32_t widest = std::max(rc_thr_pulse_us_, rc_str_pulse_us_);
    while (rc_next_pulse_us_ + widest <= now_us) {
      if (rc_thr_pulse_us_ > 0) {
        rc_thr_decoder_.OnEdge(true, rc_next_pulse_us_);
        rc_thr_decoder_.OnEdge(false, rc_next_pulse_us_ + rc_thr_pulse_us_);
      }
      if (rc_str_pulse_us_ > 0) {
        rc_str_decoder_.OnEdge(true, rc_next_pulse_us_);
        rc_str_decoder_.OnEdge(false, rc_next_pulse_us_ + rc_str_pulse_us_);
      }
      rc_next_pulse_us_ += kRcFramePeriodUs;
    }
  }

  static constexpr uint64_t kRcFramePeriodUs = 20'000;  // 50 Гц, как приёмник

  uint32_t time_ms_{0};
  bool rc_pulse_mode_{false};
  uint32_t rc_thr_pulse_us_{0};
  uint32_t rc_str_pulse_us_{0};
  uint64_t rc_next_pulse_us_{0};
  // Таймаут декодера как на ESP32 (RC_IN_TIMEOUT_MS, LOS-368).
  static constexpr RcPulseConfig kRcPulseConfig{
      .timeout_us = config::RcInputConfig::kTimeoutMs * 1000u};
  RcPulseDecoder rc_thr_decoder_{kRcPulseConfig};
  RcPulseDecoder rc_str_decoder_{kRcPulseConfig};
  bool log_to_stderr_{false};
  std::optional<ImuData> imu_data_;
  std::optional<MagData> mag_data_;
  std::optional<MagCalibData> mag_calib_;
  std::optional<RcCommand> rc_command_;
  std::optional<RcCommand> wifi_command_;
  bool identity_calib_{false};
  bool inverted_z_calib_{false};
  Failsafe failsafe_{config::FailsafeConfig::kTimeoutMs};
  DriveMode drive_mode_{DriveMode::Normal};
  float speed_limit_ms_{0.0f};
  bool stabilize_{false};
  bool kids_accel_limit_{true};
  bool kids_anti_spin_{true};
  std::optional<OversteerConfig> oversteer_config_;
  bool pitch_comp_enabled_{false};
  std::optional<float> roll_comp_gain_;
  std::optional<float> slip_target_deg_;
  KidsPreset kids_preset_{KidsPreset::Custom};
  std::optional<float> madgwick_beta_;
  std::optional<float> lpf_cutoff_hz_;
  std::optional<std::array<float, 2>> com_offset_;

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
  // LOS-216: необязательные поля 18-19 — сырые ширины RC-импульсов [мкс]
  // (0 = канал молчит). Если заданы, rc_present/rc_thr/rc_str игнорируются:
  // команда получается из импульсов через RcPulseDecoder.
  bool rc_pulses_present{false};
  uint32_t rc_thr_pulse_us{0};
  uint32_t rc_str_pulse_us{0};
};

/**
 * @brief Распарсить строку входного кадра.
 *
 * Формат (17 полей через запятую):
 *   dt_ms, ax,ay,az, gx,gy,gz,
 *   mag_present,mx,my,mz, rc_present,rc_thr,rc_str,
 * wifi_present,wifi_thr,wifi_str Единицы: accel g, gyro dps, mag мГс (как
 * ImuData/MagData).
 * Необязательно (LOS-216) ещё 2 поля: rc_thr_pulse_us, rc_str_pulse_us —
 * режим сырых RC-импульсов (см. StdioPlatform::SetRcPulses).
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
 * @param com_calib_active Идёт калибровка offset IMU от центра масс
 *                    (--start-com-calib у sim_host, LOS-384). Сам offset
 *                    берётся из snap.calib_data.
 */
[[nodiscard]] std::string FormatOutputLine(const TelemetrySnapshot& snap,
                                           float throttle, float steering,
                                           bool neutral,
                                           bool test_active = false,
                                           bool com_calib_active = false);

}  // namespace sim
}  // namespace rc_vehicle
