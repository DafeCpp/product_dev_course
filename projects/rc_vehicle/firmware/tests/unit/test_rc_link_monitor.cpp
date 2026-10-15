#include <gtest/gtest.h>

#include <vector>

#include "rc_link_monitor.hpp"
#include "rc_pulse_decoder.hpp"
#include "telemetry_event_log.hpp"

using namespace rc_vehicle;

// LOS-216, часть 3: RcLinkMonitor решает, что и когда сообщить о RC-связи.
// Время логическое (мс); опрос RC — каждые 20 мс, как RcInputHandler.

namespace {

constexpr uint32_t kPollMs = 20;

/** Два настоящих декодера + логическое время: импульсы раз в 20 мс. */
class RcBench {
 public:
  /** Прогнать dt_ms, выдавая импульсы заданной ширины (0 — канал молчит). */
  void Run(uint32_t dt_ms, uint32_t thr_us, uint32_t str_us) {
    const uint64_t end_us = now_us_ + uint64_t{dt_ms} * 1000;
    while (next_pulse_us_ + 3000 <= end_us) {
      if (thr_us) Pulse(thr_, next_pulse_us_, thr_us);
      if (str_us) Pulse(str_, next_pulse_us_, str_us);
      next_pulse_us_ += 20'000;
    }
    now_us_ = end_us;
  }

  [[nodiscard]] uint32_t NowMs() const {
    return static_cast<uint32_t>(now_us_ / 1000);
  }

  [[nodiscard]] bool Active() const {
    return thr_.IsActive(now_us_) && str_.IsActive(now_us_);
  }

  [[nodiscard]] RcInputStats Stats() const {
    return RcInputStats{.throttle = thr_.Status(now_us_),
                        .steering = str_.Status(now_us_)};
  }

 private:
  static void Pulse(RcPulseDecoder& d, uint64_t t_us, uint32_t w_us) {
    d.OnEdge(true, t_us);
    d.OnEdge(false, t_us + w_us);
  }

  RcPulseDecoder thr_{};
  RcPulseDecoder str_{};
  uint64_t now_us_{0};
  uint64_t next_pulse_us_{0};
};

/** Прогнать монитор по стенду: duration_ms с опросом каждые 20 мс. */
std::vector<RcLinkEvent> Drive(RcLinkMonitor& mon, RcBench& bench,
                               uint32_t duration_ms, uint32_t thr_us,
                               uint32_t str_us) {
  std::vector<RcLinkEvent> out;
  for (uint32_t t = 0; t < duration_ms; t += kPollMs) {
    bench.Run(kPollMs, thr_us, str_us);
    if (auto e = mon.Update(bench.NowMs(), bench.Active(), bench.Stats())) {
      out.push_back(*e);
    }
  }
  return out;
}

}  // namespace

TEST(RcLinkMonitor, SignalAtBootReportsAcquiredOnce) {
  RcLinkMonitor mon;
  RcBench bench;
  const auto events = Drive(mon, bench, 3000, 1500, 1500);
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].kind, RcLinkEvent::Kind::Restored);
  EXPECT_TRUE(events[0].since_boot);
  EXPECT_EQ(events[0].loss_count, 0u);
  EXPECT_EQ(mon.LossCount(), 0u);
}

TEST(RcLinkMonitor, NoSignalAtBootReportedOnceAfterGrace) {
  RcLinkMonitor mon(RcLinkMonitor::Config{.startup_grace_ms = 1000});
  RcBench bench;
  const auto events = Drive(mon, bench, 5000, 0, 0);
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].kind, RcLinkEvent::Kind::Lost);
  EXPECT_TRUE(events[0].since_boot);
  EXPECT_GE(events[0].ts_ms, 1000u);
  EXPECT_LT(events[0].ts_ms, 1100u);
  EXPECT_EQ(events[0].throttle_cause, RcLossCause::NoPulses);
  EXPECT_EQ(events[0].steering_cause, RcLossCause::NoPulses);
  // «С момента старта» — не потеря связи, которая была.
  EXPECT_EQ(mon.LossCount(), 0u);
}

TEST(RcLinkMonitor, LateReceiverAfterNoSignalAtBoot) {
  RcLinkMonitor mon;
  RcBench bench;
  auto events = Drive(mon, bench, 2000, 0, 0);
  ASSERT_EQ(events.size(), 1u);
  events = Drive(mon, bench, 1000, 1500, 1500);
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].kind, RcLinkEvent::Kind::Restored);
  EXPECT_TRUE(events[0].since_boot);
}

TEST(RcLinkMonitor, LossThenRestoreReportsCauseAndOutage) {
  RcLinkMonitor mon;
  RcBench bench;
  Drive(mon, bench, 2000, 1500, 1500);

  // Пульт выключили: импульсов нет на обоих каналах.
  auto events = Drive(mon, bench, 1000, 0, 0);
  ASSERT_EQ(events.size(), 1u);
  const RcLinkEvent& lost = events[0];
  EXPECT_EQ(lost.kind, RcLinkEvent::Kind::Lost);
  EXPECT_FALSE(lost.since_boot);
  EXPECT_EQ(lost.loss_count, 1u);
  EXPECT_EQ(lost.throttle_cause, RcLossCause::NoPulses);
  EXPECT_EQ(lost.steering_cause, RcLossCause::NoPulses);
  ASSERT_TRUE(lost.stats_available);
  EXPECT_GT(lost.stats.throttle.stats.accepted, 0u);
  // Декодер держит канал timeout_us (250 мс) после последнего импульса.
  EXPECT_GE(lost.ts_ms, 2000u + 230u);
  EXPECT_LE(lost.ts_ms, 2000u + 300u);

  events = Drive(mon, bench, 2000, 1500, 1500);
  ASSERT_EQ(events.size(), 1u);
  const RcLinkEvent& back = events[0];
  EXPECT_EQ(back.kind, RcLinkEvent::Kind::Restored);
  EXPECT_FALSE(back.since_boot);
  EXPECT_EQ(back.loss_count, 1u);
  // Сигнал вернулся на 3000 мс, потеря зафиксирована на ~2250 мс.
  EXPECT_GE(back.outage_ms, 700u);
  EXPECT_LE(back.outage_ms, 800u);
  EXPECT_EQ(back.suppressed, 0u);
  EXPECT_EQ(mon.LossCount(), 1u);
}

// Тот самый сценарий LOS-216 с другой стороны: импульсы идут, но шире окна
// приёма (2310 > 2000 + 200) — причина должна быть «вне окна», а не «нет
// импульсов», и в событии видна ширина отброшенного импульса.
TEST(RcLinkMonitor, OutOfRangePulsesClassifiedPerChannel) {
  RcLinkMonitor mon;
  RcBench bench;
  Drive(mon, bench, 2000, 1500, 1500);

  const auto events = Drive(mon, bench, 1000, 2310, 1500);
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].kind, RcLinkEvent::Kind::Lost);
  EXPECT_EQ(events[0].throttle_cause, RcLossCause::PulsesOutOfRange);
  EXPECT_EQ(events[0].steering_cause, RcLossCause::None);
  EXPECT_EQ(events[0].stats.throttle.stats.last_rejected_width_us, 2310u);
  EXPECT_GT(events[0].stats.throttle.stats.rejected_long, 0u);
}

// После PR 2/3 импульс у упора (2010 мкс) не должен приводить к потере.
TEST(RcLinkMonitor, StickAtEndStopDoesNotLoseLink) {
  RcLinkMonitor mon;
  RcBench bench;
  Drive(mon, bench, 1000, 1500, 1500);
  const auto events = Drive(mon, bench, 5000, 2010, 990);
  EXPECT_TRUE(events.empty());
  EXPECT_EQ(mon.LossCount(), 0u);
}

TEST(RcLinkMonitor, ShortGlitchPulsesClassifiedAsOutOfRange) {
  RcLinkMonitor mon;
  RcBench bench;
  Drive(mon, bench, 1000, 1500, 1500);
  const auto events = Drive(mon, bench, 1000, 1500, 300);
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].throttle_cause, RcLossCause::None);
  EXPECT_EQ(events[0].steering_cause, RcLossCause::PulsesOutOfRange);
  EXPECT_GT(events[0].stats.steering.stats.rejected_short, 0u);
}

TEST(RcLinkMonitor, NoStatsGivesUnknownCause) {
  RcLinkMonitor mon;
  uint32_t t = 0;
  for (; t < 500; t += kPollMs) (void)mon.Update(t, true, std::nullopt);
  std::optional<RcLinkEvent> lost;
  for (; t < 1500 && !lost; t += kPollMs) lost = mon.Update(t, false, {});
  ASSERT_TRUE(lost.has_value());
  EXPECT_EQ(lost->kind, RcLinkEvent::Kind::Lost);
  EXPECT_FALSE(lost->stats_available);
  EXPECT_EQ(lost->throttle_cause, RcLossCause::Unknown);
  EXPECT_EQ(lost->steering_cause, RcLossCause::Unknown);
}

// Дребезг: связь мигает каждые 40 мс полсекунды. В лог — не больше одного
// сообщения в секунду, пропущенное считается, финальное состояние сообщается.
TEST(RcLinkMonitor, FlappingIsRateLimitedAndFinalStateReported) {
  RcLinkMonitor mon(RcLinkMonitor::Config{.startup_grace_ms = 1000,
                                          .min_report_interval_ms = 1000});
  std::vector<RcLinkEvent> events;
  uint32_t t = 0;
  auto feed = [&](bool active) {
    if (auto e = mon.Update(t, active, std::nullopt)) events.push_back(*e);
    t += kPollMs;
  };
  for (int i = 0; i < 100; ++i) feed(true);  // 2 с стабильно
  ASSERT_EQ(events.size(), 1u);              // acquired
  events.clear();

  for (int i = 0; i < 25; ++i) feed(i % 2 != 0);  // 25 переходов, итог lost
  for (int i = 0; i < 20; ++i) feed(false);       // держится lost
  for (int i = 0; i < 100; ++i) feed(true);       // восстановилась

  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(events[0].kind, RcLinkEvent::Kind::Lost);
  EXPECT_EQ(events[0].ts_ms, 2000u);
  EXPECT_EQ(events[0].suppressed, 0u);
  EXPECT_EQ(events[1].kind, RcLinkEvent::Kind::Restored);
  EXPECT_GE(events[1].ts_ms - events[0].ts_ms, 1000u);
  // 24 перехода внутри дребезга + 1 финальный, о котором и сообщаем.
  EXPECT_EQ(events[1].suppressed, 24u);
  // Все реальные потери посчитаны, даже не попавшие в лог.
  EXPECT_EQ(mon.LossCount(), 13u);
}

TEST(RcLinkMonitor, FlapBackToReportedStateIsSilent) {
  RcLinkMonitor mon;
  uint32_t t = 0;
  for (; t < 2000; t += kPollMs) (void)mon.Update(t, true, std::nullopt);
  // Захват сообщён на t=0, интервал давно истёк — потеря сообщается сразу.
  auto lost = mon.Update(t, false, std::nullopt);
  ASSERT_TRUE(lost.has_value());
  t += kPollMs;
  // Восстановление через 20 мс — внутри интервала, молчим.
  EXPECT_FALSE(mon.Update(t, true, std::nullopt).has_value());
  t += kPollMs;
  // Ещё одна короткая потеря — состояние совпадает с сообщённым (Lost),
  // но интервал не истёк: молчим.
  EXPECT_FALSE(mon.Update(t, false, std::nullopt).has_value());
  t += kPollMs;
  // Восстановились окончательно — после интервала будет одно Restored
  // с учётом подавленных переходов.
  std::optional<RcLinkEvent> back;
  for (; t < 5000 && !back; t += kPollMs) {
    back = mon.Update(t, true, std::nullopt);
  }
  ASSERT_TRUE(back.has_value());
  EXPECT_EQ(back->kind, RcLinkEvent::Kind::Restored);
  EXPECT_EQ(back->suppressed, 2u);
  EXPECT_EQ(mon.LossCount(), 2u);
}

TEST(RcLinkMonitor, FormatLostMentionsCauseAndWidths) {
  RcLinkEvent e{};
  e.kind = RcLinkEvent::Kind::Lost;
  e.ts_ms = 1234;
  e.loss_count = 2;
  e.throttle_cause = RcLossCause::PulsesOutOfRange;
  e.steering_cause = RcLossCause::NoPulses;
  e.stats_available = true;
  e.stats.throttle.stats = {.accepted = 10,
                            .rejected_short = 0,
                            .rejected_long = 7,
                            .last_width_us = 1990,
                            .last_rejected_width_us = 2310};
  e.suppressed = 3;
  const std::string s = FormatRcLinkEvent(e);
  EXPECT_NE(s.find("RC LOST #2"), std::string::npos) << s;
  EXPECT_NE(s.find("thr: out of range 2310us"), std::string::npos) << s;
  EXPECT_NE(s.find("long=7"), std::string::npos) << s;
  EXPECT_NE(s.find("str: no pulses"), std::string::npos) << s;
  EXPECT_NE(s.find("+3 flaps unlogged"), std::string::npos) << s;
  // Строка должна влезть в буфер Log() на ESP32 (256 байт).
  EXPECT_LT(s.size(), 256u) << s;
}

TEST(RcLinkMonitor, FormatWorstCaseFitsEsp32LogBuffer) {
  constexpr uint32_t kMax = 0xFFFFFFFFu;
  RcLinkEvent e{};
  e.kind = RcLinkEvent::Kind::Lost;
  e.ts_ms = kMax;
  e.loss_count = kMax;
  e.suppressed = kMax;
  e.throttle_cause = RcLossCause::PulsesOutOfRange;
  e.steering_cause = RcLossCause::PulsesOutOfRange;
  e.stats_available = true;
  e.stats.throttle.stats = {kMax, kMax, kMax, kMax, kMax};
  e.stats.steering.stats = {kMax, kMax, kMax, kMax, kMax};
  EXPECT_LT(FormatRcLinkEvent(e).size(), 256u) << FormatRcLinkEvent(e);
}

TEST(RcLinkMonitor, FormatRestoredAndAcquired) {
  RcLinkEvent e{};
  e.kind = RcLinkEvent::Kind::Restored;
  e.outage_ms = 734;
  e.loss_count = 1;
  e.stats_available = true;
  e.stats.throttle.stats.last_width_us = 1502;
  e.stats.steering.stats.last_width_us = 1497;
  std::string s = FormatRcLinkEvent(e);
  EXPECT_NE(s.find("RC restored after 734 ms"), std::string::npos) << s;
  EXPECT_NE(s.find("thr=1502 us, str=1497 us"), std::string::npos) << s;

  e.since_boot = true;
  s = FormatRcLinkEvent(e);
  EXPECT_NE(s.find("RC signal acquired"), std::string::npos) << s;
}

TEST(RcLostParam, PacksCausesAndBootFlag) {
  EXPECT_EQ(RcLostParam(1, 2, false), 0x09);
  EXPECT_EQ(RcLostParam(2, 0, true), 0x12);
  EXPECT_EQ(RcLostParam(3, 3, true), 0x1F);
}
