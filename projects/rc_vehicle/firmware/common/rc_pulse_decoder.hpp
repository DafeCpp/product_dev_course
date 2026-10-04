#pragma once

#include <cstdint>
#include <optional>

#include "rc_vehicle_common.hpp"

namespace rc_vehicle {

/**
 * @brief Параметры декодера PWM-импульса RC-приёмника.
 *
 * Принимаются импульсы шириной [min_us - tolerance_us, max_us + tolerance_us];
 * в нормализованное значение они переводятся относительно [min_us, max_us] и
 * клампятся. Допуск нужен потому, что пульты с триммером/EPA и край хода дают
 * ~980..2020 мкс: жёсткое окно [min_us, max_us] отбрасывало такие импульсы
 * молча, и канал по таймауту считался потерянным (LOS-216).
 */
struct RcPulseConfig {
  uint32_t min_us{1000};
  uint32_t neutral_us{1500};
  uint32_t max_us{2000};
  uint32_t tolerance_us{200};
  uint32_t timeout_us{250'000};  ///< Нет валидного импульса дольше → потеря
};

/** @brief Счётчики для диагностики потери сигнала (LOS-216). */
struct RcPulseStats {
  uint32_t accepted{0};
  uint32_t rejected_short{0};
  uint32_t rejected_long{0};
  uint32_t last_width_us{0};           ///< Последний принятый импульс
  uint32_t last_rejected_width_us{0};  ///< Последний отброшенный импульс
};

/**
 * @brief Декодер одного канала RC PWM по фронтам сигнала.
 *
 * Не зависит от платформы: время приходит аргументом (мкс, 64 бита — без
 * переполнения за 71 минуту и без значения-«часового» 0). Синхронизацию
 * между ISR и читателем обеспечивает вызывающий (на ESP32 — spinlock): класс
 * тривиально копируется, читатель может взять копию под локом и вызвать Read
 * уже вне критической секции.
 */
class RcPulseDecoder {
 public:
  explicit RcPulseDecoder(const RcPulseConfig& cfg = {}) : cfg_(cfg) {}

  /** Обработать фронт: level=true — RISE, false — FALL. */
  void OnEdge(bool level, uint64_t now_us) {
    if (level) {
      // Повторный RISE без FALL (пропущенный фронт/глитч) — берём последний.
      rise_us_ = now_us;
      has_rise_ = true;
      return;
    }
    if (!has_rise_) return;  // FALL без RISE (старт посреди импульса)
    has_rise_ = false;

    const uint64_t width64 = now_us - rise_us_;
    const uint32_t width_us =
        width64 > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(width64);
    const uint32_t lo =
        cfg_.min_us > cfg_.tolerance_us ? cfg_.min_us - cfg_.tolerance_us : 0;
    const uint32_t hi = cfg_.max_us + cfg_.tolerance_us;

    if (width_us < lo) {
      ++stats_.rejected_short;
      stats_.last_rejected_width_us = width_us;
    } else if (width_us > hi) {
      ++stats_.rejected_long;
      stats_.last_rejected_width_us = width_us;
    } else {
      ++stats_.accepted;
      stats_.last_width_us = width_us;
      pulse_width_us_ = width_us;
      pulse_end_us_ = now_us;
      has_pulse_ = true;
    }
  }

  /** Значение [-1..1] или nullopt, если импульсов нет / сигнал потерян. */
  [[nodiscard]] std::optional<float> Read(uint64_t now_us) const {
    if (!IsActive(now_us)) return std::nullopt;
    return NormalizedFromPulseWidthUs(
        pulse_width_us_, static_cast<uint16_t>(cfg_.min_us),
        static_cast<uint16_t>(cfg_.neutral_us),
        static_cast<uint16_t>(cfg_.max_us));
  }

  /** Был ли валидный импульс не позднее timeout_us назад. */
  [[nodiscard]] bool IsActive(uint64_t now_us) const {
    return has_pulse_ && (now_us - pulse_end_us_) < cfg_.timeout_us;
  }

  [[nodiscard]] const RcPulseStats& Stats() const { return stats_; }

 private:
  RcPulseConfig cfg_;
  RcPulseStats stats_;
  bool has_rise_{false};
  bool has_pulse_{false};
  uint64_t rise_us_{0};
  uint64_t pulse_end_us_{0};
  uint32_t pulse_width_us_{0};
};

}  // namespace rc_vehicle
