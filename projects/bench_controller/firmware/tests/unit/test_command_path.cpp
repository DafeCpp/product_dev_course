// Командный тракт Ф2 (LOS-185): очередь → контур, идемпотентность по
// cmd_id, блокировки (программа, останов, защитное удержание), журнал.

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "bench_control_loop.hpp"
#include "command_slot.hpp"
#include "fixtures/fake_valve_channel.hpp"
#include "journal_spool.hpp"
#include "sim/host_platform.hpp"

namespace bench {
namespace {

constexpr uint32_t kPeriodMs = 2;

BenchControlLoop::Config MakeConfig() {
  BenchControlLoop::Config cfg{};
  cfg.period_ms = kPeriodMs;
  cfg.controller.force_gains = {.kp = 1e-4f, .max_output = 1.0f};
  cfg.controller.disp_gains = {.kp = 0.5f, .max_output = 1.0f};
  cfg.controller.output_slew_per_s = 1000.0f;
  cfg.controller.capture_ramp_s = 0.01f;
  cfg.manual.force_limit_n = 50'000.0f;
  cfg.manual.position_limit_mm = 50.0f;
  cfg.manual.force_ramp_n_per_s = 10'000.0f;
  cfg.manual.position_ramp_mm_per_s = 100.0f;
  cfg.watchdog = {.grace_ms = 20, .ramp_ms = 40};
  cfg.publish_every_ticks = 5;
  cfg.stats_refresh_ticks = 10;
  return cfg;
}

struct Rig {
  HostPlatform platform{};
  testing::FakeValveChannel valve{};
  StubSupervisoryLink link{};
  LockedCommandSlot inbox{};
  LockedSnapshotExchange exchange{};
  BenchControlLoop loop;

  Rig() : loop(MakeConfig(), platform, valve, link) {
    loop.AttachCommandInbox(&inbox);
    loop.AttachSnapshotPublisher(&exchange);
  }

  void Tick(int n = 1) {
    for (int i = 0; i < n; ++i) {
      platform.AdvanceTimeMs(kPeriodMs);
      loop.HostStep(kPeriodMs);
    }
  }

  /// Отправить команду и дать контуру её забрать; вернуть последний ack
  CommandAck Send(const BenchCommand& cmd) {
    inbox.Post(cmd);
    Tick();
    Tick(4);  // добить до публикации (publish_every_ticks = 5)
    const BenchStateSnapshot s = exchange.Latest();
    return s.acks[s.ack_count - 1];
  }

  BenchStateSnapshot Snap() {
    Tick(5);
    return exchange.Latest();
  }
};

BenchCommand Cmd(uint32_t id, CommandType type) {
  return BenchCommand{.cmd_id = id, .type = type};
}
BenchCommand SetSp(uint32_t id, float v) {
  return BenchCommand{
      .cmd_id = id, .type = CommandType::kSetSetpoint, .value = v};
}
BenchCommand SetMode(uint32_t id, ControlMode m) {
  return BenchCommand{.cmd_id = id, .type = CommandType::kSetMode, .mode = m};
}
BenchCommand Enable(uint32_t id, bool on) {
  return BenchCommand{.cmd_id = id, .type = CommandType::kEnable, .enable = on};
}

TEST(CommandPath, SetpointReachesValveThroughRamp) {
  Rig rig;
  const CommandAck ack = rig.Send(SetSp(1, 10.0f));
  EXPECT_EQ(ack.cmd_id, 1u);
  EXPECT_EQ(ack.result, CommandResult::kAccepted);
  EXPECT_FALSE(ack.duplicate);

  rig.Tick(10);
  const BenchStateSnapshot s = rig.Snap();
  EXPECT_FLOAT_EQ(s.manual_commanded, 10.0f);
  // Рампа 100 мм/с: за ~40 мс уставка ещё не дошла до 10 мм
  EXPECT_GT(s.manual_ramped, 0.0f);
  EXPECT_LT(s.manual_ramped, 10.0f);
  EXPECT_GT(rig.valve.Last().value,
            0.0f);  // позиция 0 < цели → открываем клапан

  rig.Tick(100);  // 200 мс: рампа 100 мм/с → дошли
  EXPECT_FLOAT_EQ(rig.Snap().manual_ramped, 10.0f);
}

TEST(CommandPath, DuplicateCmdIdAcksWithoutReexecution) {
  Rig rig;
  EXPECT_EQ(rig.Send(SetSp(1, 10.0f)).result, CommandResult::kAccepted);
  EXPECT_EQ(rig.Send(SetSp(2, 3.0f)).result, CommandResult::kAccepted);

  // Повтор id = 1 с другой уставкой: ack исходный, уставка не меняется
  const CommandAck dup = rig.Send(SetSp(1, 40.0f));
  EXPECT_TRUE(dup.duplicate);
  EXPECT_EQ(dup.result, CommandResult::kAccepted);
  EXPECT_EQ(dup.cmd_id, 1u);

  const BenchStateSnapshot s = rig.Snap();
  EXPECT_FLOAT_EQ(s.manual_commanded, 3.0f);
  EXPECT_EQ(s.cmd_total, 3u);
  EXPECT_EQ(s.cmd_duplicate, 1u);
}

TEST(CommandPath, DuplicateOfRejectedCommandStaysRejected) {
  Rig rig;
  EXPECT_EQ(rig.Send(SetSp(5, 1e9f)).reason, RejectReason::kOutOfRange);
  const CommandAck dup = rig.Send(SetSp(5, 1.0f));
  EXPECT_TRUE(dup.duplicate);
  EXPECT_EQ(dup.result, CommandResult::kRejected);
  EXPECT_EQ(dup.reason, RejectReason::kOutOfRange);
}

TEST(CommandPath, ZeroIdIsRejectedAndNotRemembered) {
  Rig rig;
  const CommandAck a = rig.Send(SetSp(0, 1.0f));
  EXPECT_EQ(a.result, CommandResult::kRejected);
  EXPECT_EQ(a.reason, RejectReason::kInvalidId);
  const CommandAck b = rig.Send(SetSp(0, 1.0f));
  EXPECT_FALSE(b.duplicate);  // id 0 не попадает в таблицу идемпотентности
}

TEST(CommandPath, SetpointOutsideLimitsOrNanIsRejected) {
  Rig rig;
  EXPECT_EQ(rig.Send(SetSp(1, 50.1f)).reason, RejectReason::kOutOfRange);
  EXPECT_EQ(rig.Send(SetSp(2, -50.1f)).reason, RejectReason::kOutOfRange);
  EXPECT_EQ(rig.Send(SetSp(3, std::numeric_limits<float>::quiet_NaN())).reason,
            RejectReason::kOutOfRange);
  EXPECT_EQ(rig.Send(SetSp(4, 50.0f)).result, CommandResult::kAccepted);
  EXPECT_EQ(rig.Snap().cmd_rejected, 3u);
}

TEST(CommandPath, SetModeCapturesCurrentValueBumpless) {
  Rig rig;
  rig.valve.feedback.force_n = 1200.0f;
  EXPECT_EQ(rig.Send(SetMode(1, ControlMode::kForce)).result,
            CommandResult::kAccepted);
  const BenchStateSnapshot s = rig.Snap();
  EXPECT_EQ(s.mode, ControlMode::kForce);
  EXPECT_FLOAT_EQ(s.manual_commanded, 1200.0f);  // уставка = текущее усилие
  EXPECT_FLOAT_EQ(s.manual_ramped, 1200.0f);
}

TEST(CommandPath, SetpointLimitDependsOnActiveMode) {
  Rig rig;
  EXPECT_EQ(rig.Send(SetSp(1, 5000.0f)).reason, RejectReason::kOutOfRange);
  (void)rig.Send(SetMode(2, ControlMode::kForce));
  EXPECT_EQ(rig.Send(SetSp(3, 5000.0f)).result, CommandResult::kAccepted);
}

TEST(CommandPath, ManualCommandsBlockedWhileProgramRuns) {
  Rig rig;
  const SineProgram::Segment seg[] = {
      {.mean = 1000.0f, .amplitude = 100.0f, .freq_hz = 1.0f, .cycles = 1000}};
  rig.loop.SetProgram(seg);

  EXPECT_EQ(rig.Send(SetSp(1, 1.0f)).reason, RejectReason::kProgramRunning);
  EXPECT_EQ(rig.Send(SetMode(2, ControlMode::kDisplacement)).reason,
            RejectReason::kProgramRunning);
  EXPECT_EQ(rig.Send(Enable(3, true)).reason, RejectReason::kProgramRunning);
  EXPECT_TRUE(rig.Snap().program_active);
}

TEST(CommandPath, ControlledStopEndsProgramAndHoldsPosition) {
  Rig rig;
  const SineProgram::Segment seg[] = {
      {.mean = 1000.0f, .amplitude = 100.0f, .freq_hz = 1.0f, .cycles = 1000}};
  rig.loop.SetProgram(seg);
  rig.valve.feedback.position_mm = 7.0f;

  EXPECT_EQ(rig.Send(Cmd(1, CommandType::kControlledStop)).result,
            CommandResult::kAccepted);
  BenchStateSnapshot s = rig.Snap();
  EXPECT_FALSE(s.program_active);
  EXPECT_TRUE(s.holding);
  EXPECT_EQ(s.mode, ControlMode::kDisplacement);
  EXPECT_EQ(s.last_stop_reason, StopReason::kOperatorControlledStop);
  EXPECT_TRUE(s.enabled);  // controlled_stop не снимает enable

  // Пока стоим: уставки и режим отклоняются с понятной причиной
  EXPECT_EQ(rig.Send(SetSp(2, 1.0f)).reason, RejectReason::kStopped);
  EXPECT_EQ(rig.Send(SetMode(3, ControlMode::kForce)).reason,
            RejectReason::kStopped);

  // enable(true) возобновляет ручное управление с текущей позиции
  EXPECT_EQ(rig.Send(Enable(4, true)).result, CommandResult::kAccepted);
  s = rig.Snap();
  EXPECT_FALSE(s.holding);
  EXPECT_FLOAT_EQ(s.manual_commanded, 7.0f);
  EXPECT_EQ(rig.Send(SetSp(5, 9.0f)).result, CommandResult::kAccepted);
}

TEST(CommandPath, ControlledStopTwiceIsHarmless) {
  Rig rig;
  EXPECT_EQ(rig.Send(Cmd(1, CommandType::kControlledStop)).result,
            CommandResult::kAccepted);
  EXPECT_EQ(rig.Send(Cmd(2, CommandType::kControlledStop)).result,
            CommandResult::kAccepted);
}

TEST(CommandPath, EStopSoftDisablesOutputAndIsLabelledSoft) {
  Rig rig;
  EXPECT_EQ(rig.Send(Cmd(1, CommandType::kEStopSoft)).result,
            CommandResult::kAccepted);
  const BenchStateSnapshot s = rig.Snap();
  EXPECT_FALSE(s.enabled);
  EXPECT_TRUE(s.holding);
  EXPECT_EQ(s.last_stop_reason, StopReason::kOperatorEStopSoft);
  EXPECT_FALSE(rig.valve.Last().enable);  // клапан получил enable = false

  EXPECT_EQ(rig.Send(Enable(2, true)).result, CommandResult::kAccepted);
  EXPECT_TRUE(rig.Snap().enabled);
  EXPECT_TRUE(rig.valve.Last().enable);
}

TEST(CommandPath, EnableFalseAlwaysAccepted) {
  Rig rig;
  EXPECT_EQ(rig.Send(Enable(1, false)).result, CommandResult::kAccepted);
  EXPECT_FALSE(rig.Snap().enabled);
}

TEST(CommandPath, SafetyHoldCannotBeClearedByCommand) {
  Rig rig;
  rig.link.SetAlive(false);
  rig.Tick(60);  // grace 20 мс + ramp 40 мс → SafeHold
  BenchStateSnapshot s = rig.Snap();
  ASSERT_EQ(s.link_state, LinkState::kSafeHold);
  EXPECT_TRUE(s.holding);
  EXPECT_EQ(s.last_stop_reason, StopReason::kLinkSafeHold);

  EXPECT_EQ(rig.Send(SetSp(1, 1.0f)).reason, RejectReason::kHeld);
  EXPECT_EQ(rig.Send(Enable(2, true)).reason, RejectReason::kHeld);
  EXPECT_EQ(rig.Send(Enable(3, false)).result, CommandResult::kAccepted);
}

TEST(CommandPath, SafetyHoldOverridesOperatorStop) {
  Rig rig;
  (void)rig.Send(Cmd(1, CommandType::kControlledStop));
  rig.link.SetAlive(false);
  rig.Tick(60);
  EXPECT_EQ(rig.Snap().last_stop_reason, StopReason::kLinkSafeHold);
  // Операторский resume больше не работает: причина стала защитной
  EXPECT_EQ(rig.Send(Enable(2, true)).reason, RejectReason::kHeld);
}

TEST(CommandPath, SpecimenFailureHoldBlocksManualCommands) {
  Rig rig;
  (void)rig.Send(SetMode(1, ControlMode::kForce));
  (void)rig.Send(SetSp(2, 20'000.0f));
  // Детектор взводится после выхода на режим (сила ≈ уставка), затем
  // сила проваливается (образец «порван») и детектор защёлкивается.
  for (int i = 0; i < 600; ++i) {
    rig.valve.feedback.force_n = rig.loop.Controller().EffectiveTarget();
    rig.Tick();
  }
  rig.valve.feedback.force_n = 100.0f;
  rig.Tick(50);
  const BenchStateSnapshot s = rig.Snap();
  EXPECT_TRUE(s.failure_latched);
  EXPECT_TRUE(s.holding);
  EXPECT_EQ(s.last_stop_reason, StopReason::kSpecimenFailure);
  EXPECT_EQ(rig.Send(SetSp(3, 1.0f)).reason, RejectReason::kHeld);
}

TEST(CommandPath, SlotOverwriteDropsOlderCommandAndCountsIt) {
  Rig rig;
  rig.inbox.Post(SetSp(1, 11.0f));
  rig.inbox.Post(
      SetSp(2, 22.0f));  // перезаписал id = 1 до того, как контур забрал
  rig.Tick(5);
  const BenchStateSnapshot s = rig.exchange.Latest();
  EXPECT_FLOAT_EQ(s.manual_commanded, 22.0f);
  EXPECT_EQ(s.cmd_total, 1u);
  EXPECT_EQ(s.cmd_overwritten, 1u);
}

TEST(CommandPath, AckTailKeepsLastFourOldestFirst) {
  Rig rig;
  for (uint32_t id = 1; id <= 6; ++id) (void)rig.Send(SetSp(id, 1.0f));
  const BenchStateSnapshot s = rig.Snap();
  ASSERT_EQ(s.ack_count, BenchStateSnapshot::kAckTail);
  EXPECT_EQ(s.acks[0].cmd_id, 3u);
  EXPECT_EQ(s.acks[3].cmd_id, 6u);
}

TEST(CommandPath, EveryCommandAndTransitionIsJournaled) {
  Rig rig;
  (void)rig.Send(SetSp(1, 5.0f));
  (void)rig.Send(SetSp(1, 5.0f));  // дубль тоже в журнале
  (void)rig.Send(SetSp(2, 1e9f));  // nack тоже
  (void)rig.Send(Cmd(3, CommandType::kEStopSoft));

  const BenchStateSnapshot s = rig.Snap();
  ASSERT_GE(s.journal_count, 5);
  EXPECT_EQ(s.journal_total, s.journal_count);

  // Порядок: set_setpoint ok, дубль, nack, hold, e_stop_soft. Удержание
  // применяется во время исполнения команды и пишется раньше её записи.
  EXPECT_EQ(s.journal[0].kind, JournalKind::kCommand);
  EXPECT_EQ(s.journal[0].cmd_id, 1u);
  EXPECT_EQ(s.journal[0].flags & 1, 0);
  EXPECT_EQ(s.journal[1].flags & 1, 1);  // duplicate
  EXPECT_EQ(s.journal[2].arg1, static_cast<uint8_t>(CommandResult::kRejected));
  EXPECT_EQ(s.journal[3].kind, JournalKind::kHold);
  EXPECT_EQ(s.journal[3].arg0,
            static_cast<uint8_t>(StopReason::kOperatorEStopSoft));
  EXPECT_EQ(s.journal[4].kind, JournalKind::kCommand);
  EXPECT_EQ(s.journal[4].arg0, static_cast<uint8_t>(CommandType::kEStopSoft));
  for (uint8_t i = 1; i < s.journal_count; ++i) {
    EXPECT_EQ(s.journal[i].seq, s.journal[i - 1].seq + 1);
  }
}

TEST(CommandPath, LinkStateTransitionsAreJournaled) {
  Rig rig;
  rig.link.SetAlive(false);
  rig.Tick(60);
  const BenchStateSnapshot s = rig.Snap();
  int transitions = 0;
  for (uint8_t i = 0; i < s.journal_count; ++i) {
    if (s.journal[i].kind == JournalKind::kLinkState) ++transitions;
  }
  EXPECT_EQ(transitions, 3);  // running→grace→ramp_down→safe_hold
}

TEST(CommandPath, JournalKeepsOnlyLastEntriesButCountsAll) {
  Rig rig;
  for (uint32_t id = 1; id <= 40; ++id) (void)rig.Send(SetSp(id, 1.0f));
  const BenchStateSnapshot s = rig.Snap();
  EXPECT_EQ(s.journal_total, 40u);
  EXPECT_EQ(s.journal_count, BenchStateSnapshot::kJournalTail);
  EXPECT_EQ(s.journal[s.journal_count - 1].cmd_id, 40u);
}

TEST(CommandPath, SnapshotIsPublishedEveryNTicksOnly) {
  Rig rig;
  rig.Tick(4);
  EXPECT_EQ(rig.exchange.PublishedCount(), 0u);
  rig.Tick(1);
  EXPECT_EQ(rig.exchange.PublishedCount(), 1u);
  rig.Tick(10);
  EXPECT_EQ(rig.exchange.PublishedCount(), 3u);
}

TEST(CommandPath, SnapshotCarriesTickStatsAndEstopState) {
  Rig rig;
  rig.Tick(30);
  const BenchStateSnapshot s = rig.exchange.Latest();
  EXPECT_GT(s.tick.count, 0u);
  EXPECT_EQ(s.tick.min_us, 2000u);
  EXPECT_EQ(s.estop_chain, EstopChainState::kUnknown);  // источника нет
}

TEST(CommandPath, LoopWorksWithoutInboxAndPublisher) {
  HostPlatform platform;
  testing::FakeValveChannel valve;
  StubSupervisoryLink link;
  BenchControlLoop loop(MakeConfig(), platform, valve, link);
  for (int i = 0; i < 20; ++i) {
    platform.AdvanceTimeMs(kPeriodMs);
    (void)loop.HostStep(kPeriodMs);
  }
  EXPECT_FALSE(valve.setpoints.empty());
}

class SpoolPlatform final : public BenchPlatform {
 public:
  [[nodiscard]] uint32_t GetTimeMs() const noexcept override { return 0; }
  [[nodiscard]] uint64_t GetTimeUs() const noexcept override { return 0; }
  void Log(LogLevel, std::string_view) const override {}
  [[nodiscard]] std::expected<void, PlatformError> CreateControlTask(
      void (*)(void*), void*) override {
    return {};
  }
  void DelayUntilNextTick(uint32_t) override {}
  bool PersistJournalEntry(const JournalEntry& e) override {
    if (fail) return false;
    saved.push_back(e.seq);
    return true;
  }
  bool fail{false};
  std::vector<uint32_t> saved;
};

TEST(JournalSpool, PersistsEachEntryOnceAndRetriesAfterFailure) {
  Rig rig;
  (void)rig.Send(SetSp(1, 1.0f));
  (void)rig.Send(SetSp(2, 2.0f));
  const BenchStateSnapshot s = rig.Snap();

  SpoolPlatform platform;
  JournalSpool spool;
  platform.fail = true;
  EXPECT_EQ(spool.Spool(s, platform), 0u);
  platform.fail = false;
  EXPECT_EQ(spool.Spool(s, platform), 2u);
  EXPECT_EQ(spool.Spool(s, platform), 0u);  // повторно не пишем
  ASSERT_EQ(platform.saved.size(), 2u);
  EXPECT_EQ(platform.saved[0], 0u);
  EXPECT_EQ(platform.saved[1], 1u);
}

}  // namespace
}  // namespace bench
