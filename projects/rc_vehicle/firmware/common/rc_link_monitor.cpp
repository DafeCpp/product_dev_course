#include "rc_link_monitor.hpp"

#include "log_format.hpp"

namespace rc_vehicle {

namespace {

RcLossCause ChannelCause(const RcChannelStatus& ch,
                         const RcPulseStats& baseline) {
  if (ch.active) return RcLossCause::None;
  return ch.stats.Rejected() > baseline.Rejected()
             ? RcLossCause::PulsesOutOfRange
             : RcLossCause::NoPulses;
}

void FormatChannel(LogFormat& fmt, const char* name, RcLossCause cause,
                   const RcChannelStatus& ch) {
  // Компактно: худший случай (все счётчики uint32 max) должен влезть в
  // 256-байтный буфер Log() на ESP32.
  fmt << " " << name << ": " << RcLossCauseName(cause);
  if (cause == RcLossCause::PulsesOutOfRange) {
    fmt << " " << ch.stats.last_rejected_width_us << "us";
  }
  fmt << " acc=" << ch.stats.accepted << " short=" << ch.stats.rejected_short
      << " long=" << ch.stats.rejected_long;
}

}  // namespace

const char* RcLossCauseName(RcLossCause cause) {
  switch (cause) {
    case RcLossCause::None:
      return "ok";
    case RcLossCause::NoPulses:
      return "no pulses";
    case RcLossCause::PulsesOutOfRange:
      return "out of range";
    case RcLossCause::Unknown:
      return "unknown";
  }
  return "unknown";
}

std::optional<RcLinkEvent> RcLinkMonitor::Update(
    uint32_t now_ms, bool active, const std::optional<RcInputStats>& stats) {
  if (!started_) {
    started_ = true;
    start_ms_ = now_ms;
  }

  if (stats) {
    if (stats->throttle.active) thr_baseline_ = stats->throttle.stats;
    if (stats->steering.active) str_baseline_ = stats->steering.stats;
  }

  if (active != active_) {
    active_ = active;
    if (reported_ != Reported::Unknown) ++transitions_since_report_;
    if (active) {
      ever_active_ = true;
    } else if (ever_active_) {
      ++loss_count_;
      lost_since_ms_ = now_ms;
    }
    if (active && loss_count_ > 0) restored_at_ms_ = now_ms;
  }

  const Reported want = active_ ? Reported::Active : Reported::Lost;
  if (reported_ == Reported::Unknown) {
    if (!active_ && now_ms - start_ms_ < cfg_.startup_grace_ms) {
      return std::nullopt;
    }
  } else if (want == reported_ ||
             now_ms - last_report_ms_ < cfg_.min_report_interval_ms) {
    return std::nullopt;
  }

  RcLinkEvent evt = MakeEvent(now_ms, active_, stats);
  reported_ = want;
  last_report_ms_ = now_ms;
  transitions_since_report_ = 0;
  return evt;
}

RcLinkEvent RcLinkMonitor::MakeEvent(uint32_t now_ms, bool active,
                                     const std::optional<RcInputStats>& stats) {
  RcLinkEvent evt{};
  evt.ts_ms = now_ms;
  evt.loss_count = loss_count_;
  // Сам переход, о котором сообщаем, — не «подавленный».
  evt.suppressed =
      transitions_since_report_ > 0 ? transitions_since_report_ - 1 : 0;
  evt.stats_available = stats.has_value();
  if (stats) evt.stats = *stats;

  if (active) {
    evt.kind = RcLinkEvent::Kind::Restored;
    evt.since_boot = loss_count_ == 0;
    // Момент восстановления, а не сообщения: Restored может задержаться
    // ограничением частоты на секунду.
    evt.outage_ms = evt.since_boot ? 0 : restored_at_ms_ - lost_since_ms_;
    return evt;
  }

  evt.kind = RcLinkEvent::Kind::Lost;
  evt.since_boot = !ever_active_;
  if (stats) {
    evt.throttle_cause = ChannelCause(stats->throttle, thr_baseline_);
    evt.steering_cause = ChannelCause(stats->steering, str_baseline_);
  } else {
    evt.throttle_cause = RcLossCause::Unknown;
    evt.steering_cause = RcLossCause::Unknown;
  }
  return evt;
}

std::string FormatRcLinkEvent(const RcLinkEvent& evt) {
  LogFormat fmt;
  if (evt.kind == RcLinkEvent::Kind::Lost) {
    if (evt.since_boot) {
      fmt << "RC: no signal since boot, t=" << evt.ts_ms << "ms";
    } else {
      fmt << "RC LOST #" << evt.loss_count << " t=" << evt.ts_ms << "ms";
    }
    if (evt.stats_available) {
      fmt << ";";
      FormatChannel(fmt, "thr", evt.throttle_cause, evt.stats.throttle);
      fmt << ";";
      FormatChannel(fmt, "str", evt.steering_cause, evt.stats.steering);
    } else {
      fmt << "; cause unknown (no decoder stats)";
    }
  } else {
    if (evt.since_boot) {
      fmt << "RC signal acquired, t=" << evt.ts_ms << "ms";
    } else {
      fmt << "RC restored after " << evt.outage_ms
          << " ms (losses since boot: " << evt.loss_count << ")";
    }
    if (evt.stats_available) {
      fmt << "; thr=" << evt.stats.throttle.stats.last_width_us
          << " us, str=" << evt.stats.steering.stats.last_width_us << " us";
    }
  }
  if (evt.suppressed > 0) {
    fmt << "; +" << evt.suppressed << " flaps unlogged";
  }
  return fmt.str();
}

}  // namespace rc_vehicle
