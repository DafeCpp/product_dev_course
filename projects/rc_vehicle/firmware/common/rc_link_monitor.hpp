#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>

#include "rc_pulse_decoder.hpp"

namespace rc_vehicle {

/**
 * @brief Почему канал RC считается потерянным (LOS-216).
 *
 * Определяется по счётчикам RcPulseDecoder с момента, когда канал был
 * активен последний раз: отброшенные импульсы есть → сигнал приходит, но не
 * попадает в окно приёма; нет ни принятых, ни отброшенных → импульсов нет
 * вообще (провод, питание приёмника, бинд).
 */
enum class RcLossCause : uint8_t {
  None = 0,              ///< Канал активен
  NoPulses = 1,          ///< Импульсов нет
  PulsesOutOfRange = 2,  ///< Импульсы есть, но вне окна приёма
  Unknown = 3,           ///< Платформа не отдаёт счётчики (GetRcStats)
};

/**
 * @brief Переход состояния RC-связи — POD для очереди платформы.
 *
 * Как DiagnosticsSnapshot (LOS-252): control loop только заполняет событие,
 * форматирование строки и печать в serial — вне 500 Гц пути
 * (VehicleControlPlatform::PublishRcLinkEvent).
 */
struct RcLinkEvent {
  enum class Kind : uint8_t { Lost = 0, Restored = 1 };

  Kind kind{Kind::Lost};
  uint32_t ts_ms{0};
  /// Lost: сигнала не было ни разу с момента старта.
  /// Restored: первый захват сигнала после старта.
  bool since_boot{false};
  /// Restored: сколько длилась потеря [мс] (0 при since_boot).
  uint32_t outage_ms{0};
  /// Переходы, не попавшие в лог из-за ограничения частоты сообщений
  /// (дребезг связи) с прошлого сообщения.
  uint32_t suppressed{0};
  /// Номер потери с момента старта (Lost) / число потерь (Restored).
  uint32_t loss_count{0};
  RcLossCause throttle_cause{RcLossCause::None};
  RcLossCause steering_cause{RcLossCause::None};
  bool stats_available{false};
  RcInputStats stats{};
};

static_assert(std::is_trivially_copyable_v<RcLinkEvent>,
              "RcLinkEvent must stay trivially copyable for queue copy");

/**
 * @brief Отслеживает переходы RC-связи и решает, что сообщить (LOS-216).
 *
 * Раньше потеря RC была видна только по нулям в rc_throttle/rc_steering —
 * без момента, длительности и причины. Монитор выдаёт событие на каждом
 * значимом переходе:
 *  - Lost с причиной по каждому каналу;
 *  - Restored с длительностью потери;
 *  - при старте: Restored(since_boot) на первом захвате или
 *    Lost(since_boot), если за startup_grace_ms сигнал так и не появился.
 *
 * Дребезг связи не заливает serial: сообщения не чаще min_report_interval_ms,
 * пропущенные переходы считаются в RcLinkEvent::suppressed, а итоговое
 * состояние всё равно сообщается, как только интервал истёк.
 *
 * Чистая логика без платформы — вызывается из control loop каждую итерацию.
 */
class RcLinkMonitor {
 public:
  struct Config {
    uint32_t startup_grace_ms{1000};
    uint32_t min_report_interval_ms{1000};
  };

  RcLinkMonitor() = default;
  explicit RcLinkMonitor(const Config& cfg) : cfg_(cfg) {}

  /**
   * @param active RC-команда валидна (RcInputHandler::IsActive)
   * @param stats Счётчики декодеров, если платформа их отдаёт
   * @return Событие, которое надо сообщить, или nullopt
   */
  [[nodiscard]] std::optional<RcLinkEvent> Update(
      uint32_t now_ms, bool active, const std::optional<RcInputStats>& stats);

  /** Сколько раз связь терялась после того, как была (без since_boot). */
  [[nodiscard]] uint32_t LossCount() const noexcept { return loss_count_; }

 private:
  enum class Reported : uint8_t { Unknown, Active, Lost };

  [[nodiscard]] RcLinkEvent MakeEvent(uint32_t now_ms, bool active,
                                      const std::optional<RcInputStats>& stats);

  Config cfg_{};
  bool started_{false};
  uint32_t start_ms_{0};
  bool active_{false};
  bool ever_active_{false};
  Reported reported_{Reported::Unknown};
  uint32_t last_report_ms_{0};
  uint32_t transitions_since_report_{0};
  uint32_t lost_since_ms_{0};   ///< Начало последней потери
  uint32_t restored_at_ms_{0};  ///< Конец последней потери
  uint32_t loss_count_{0};
  // Счётчики на момент, когда канал был активен последний раз — база для
  // определения причины потери.
  RcPulseStats thr_baseline_{};
  RcPulseStats str_baseline_{};
};

/** Человекочитаемая строка для serial-лога. */
[[nodiscard]] std::string FormatRcLinkEvent(const RcLinkEvent& evt);

[[nodiscard]] const char* RcLossCauseName(RcLossCause cause);

}  // namespace rc_vehicle
