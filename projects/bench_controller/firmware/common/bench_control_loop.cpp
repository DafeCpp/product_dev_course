#include "bench_control_loop.hpp"

#include <cmath>
#include <firmware_common/slew_rate.hpp>

namespace bench {

BenchControlLoop::BenchControlLoop(const Config& config,
                                   BenchPlatform& platform,
                                   IValveChannel& valve,
                                   ISupervisoryLink& supervisory)
    : config_(config),
      platform_(platform),
      valve_(valve),
      supervisory_(supervisory),
      controller_(config.controller),
      failure_detector_(config.failure),
      link_watchdog_(config.watchdog) {}

void BenchControlLoop::SetProgram(
    std::span<const SineProgram::Segment> segments) {
  program_ = SineProgram(segments);
  // Программа задаёт усилие — контур стартует в force-режиме с
  // захватом текущего усилия (подвод в displacement — вне спайка).
  controller_.RequestMode(ControlMode::kForce, valve_.ReadFeedback().force_n);
}

std::expected<void, PlatformError> BenchControlLoop::Init() {
  last_loop_ms_ = platform_.GetTimeMs();
  return platform_.CreateControlTask(&BenchControlLoop::ControlTaskEntry, this);
}

void BenchControlLoop::ControlTaskEntry(void* arg) {
  static_cast<BenchControlLoop*>(arg)->ControlTaskLoop();
}

void BenchControlLoop::ControlTaskLoop() {
  platform_.RegisterTaskWdt();  // до первого FeedTaskWdt (esp_task_wdt_add)
  while (true) {
    platform_.DelayUntilNextTick(config_.period_ms);
    platform_.FeedTaskWdt();  // кормим WDT всегда, даже после остановки
    if (stop_requested_.load()) {
      // Прекращаем тики: Stats()/feedback-счётчики больше никто не
      // меняет — их можно безопасно снять с другого ядра.
      stopped_ack_.store(true);
      continue;
    }
    const uint32_t now = platform_.GetTimeMs();
    TickOnce(now, now - last_loop_ms_);
    last_loop_ms_ = now;
  }
}

TickSnapshot BenchControlLoop::HostStep(uint32_t dt_ms) {
  const uint32_t now = platform_.GetTimeMs();
  TickOnce(now, dt_ms);
  last_loop_ms_ = now;
  return snapshot_;
}

void BenchControlLoop::TickOnce(uint32_t now_ms, uint32_t dt_ms) {
  const uint32_t dt_us = dt_ms * 1000;
  const float dt_sec = static_cast<float>(dt_ms) / 1000.0f;

  valve_.PreTick(dt_us);
  const ValveFeedback fb = valve_.ReadFeedback();

  // --- Команда оператора (single-slot, не блокирует) ---
  BenchCommand cmd{};
  if (inbox_ != nullptr && inbox_->TryTake(cmd)) {
    ProcessCommand(now_ms, cmd, fb);
  }

  // --- Программа ---
  float program_target = program_.Step(dt_sec);
  const bool program_active = !program_.Finished();
  if (program_was_active_ && !program_active) {
    // Программа закончилась и держит финальное среднее — ручная
    // уставка стартует с него, без скачка цели.
    manual_commanded_ = program_target;
    manual_ramped_ = program_target;
  }
  program_was_active_ = program_active;

  // --- Safety: связь с платформой ---
  const LinkState link_state =
      link_watchdog_.Update(now_ms, supervisory_.IsAlive());
  if (link_state != prev_link_state_) {
    Log({.kind = JournalKind::kLinkState,
         .arg0 = static_cast<uint8_t>(prev_link_state_),
         .arg1 = static_cast<uint8_t>(link_state)},
        now_ms);
    prev_link_state_ = link_state;
  }
  if (link_state == LinkState::kRampDown) {
    // Плавная разгрузка: амплитудная часть стягивается к среднему.
    const float mean = program_.CurrentMean();
    program_target =
        mean + (program_target - mean) * link_watchdog_.RampScale(now_ms);
  } else if (link_state == LinkState::kSafeHold) {
    EnterHold(now_ms, HoldReason::kLink, StopReason::kLinkSafeHold, fb);
  }

  // --- Safety: разрушение образца (только в force-режиме) ---
  // Сравниваем с эффективной уставкой прошлого тика (после рампы
  // захвата), а не с целью программы: на стартовой рампе фактическая
  // сила ещё далека от полной программы — это не разрушение.
  if (controller_.Mode() == ControlMode::kForce &&
      failure_detector_.Update(fb.force_n, controller_.EffectiveTarget())) {
    EnterHold(now_ms, HoldReason::kFailure, StopReason::kSpecimenFailure, fb);
  }
  if (failure_detector_.Latched() && !prev_failure_latched_) {
    Log({.kind = JournalKind::kFailureLatch}, now_ms);
  }
  prev_failure_latched_ = failure_detector_.Latched();

  // --- Ручная уставка: рампа к командному значению ---
  if (!program_active && !holding_) {
    const float rate = controller_.Mode() == ControlMode::kForce
                           ? config_.manual.force_ramp_n_per_s
                           : config_.manual.position_ramp_mm_per_s;
    manual_ramped_ = firmware_common::ApplySlewRate(
        manual_commanded_, manual_ramped_, rate, dt_sec);
  }

  // --- Регулятор ---
  float target = program_target;
  if (holding_) {
    target = hold_position_mm_;
  } else if (!program_active) {
    target = manual_ramped_;
  }
  const ValveSetpoint sp = controller_.Step(target, fb, dt_sec);

  valve_.WriteSetpoint(sp);
  valve_.PostTick(dt_us);

  stats_.Record(platform_.GetTimeUs());
  snapshot_ = TickSnapshot{
      .now_ms = now_ms,
      .mode = controller_.Mode(),
      .link_state = link_state,
      .program_target = program_target,
      .effective_target = controller_.EffectiveTarget(),
      .valve_command = sp.value,
      .force_n = fb.force_n,
      .position_mm = fb.position_mm,
      .valve_status = fb.status,
      .failure_latched = failure_detector_.Latched(),
      .fb_fresh = fb.fresh,
      .fb_age_ticks = fb.age_ticks,
  };
  Publish(now_ms, snapshot_);
}

void BenchControlLoop::Log(JournalEntry entry, uint32_t now_ms) {
  entry.t_ms = now_ms;
  journal_.Append(entry);
}

void BenchControlLoop::EnterHold(uint32_t now_ms, HoldReason hold,
                                 StopReason reason, const ValveFeedback& fb) {
  if (holding_) {
    // Защитная причина перекрывает операторский останов: после неё
    // enable(true) уже не возобновляет работу.
    if (hold_reason_ == HoldReason::kOperator &&
        hold != HoldReason::kOperator) {
      hold_reason_ = hold;
      last_stop_reason_ = reason;
      Log({.kind = JournalKind::kHold, .arg0 = static_cast<uint8_t>(reason)},
          now_ms);
    }
    return;
  }
  holding_ = true;
  hold_reason_ = hold;
  last_stop_reason_ = reason;
  hold_position_mm_ = fb.position_mm;
  controller_.RequestMode(ControlMode::kDisplacement, fb.position_mm);
  Log({.kind = JournalKind::kHold, .arg0 = static_cast<uint8_t>(reason)},
      now_ms);
}

void BenchControlLoop::RecordAck(const CommandAck& ack) {
  acks_[ack_total_ % BenchStateSnapshot::kAckTail] = ack;
  ++ack_total_;
}

void BenchControlLoop::ProcessCommand(uint32_t now_ms, const BenchCommand& cmd,
                                      const ValveFeedback& fb) {
  ++cmd_total_;
  CommandAck ack{};
  bool duplicate = false;

  if (cmd.cmd_id != 0) {
    const uint32_t n =
        seen_total_ < kSeenCapacity ? seen_total_ : kSeenCapacity;
    for (uint32_t i = 0; i < n; ++i) {
      if (seen_[i].cmd_id == cmd.cmd_id) {
        ack = seen_[i];  // исходный результат, без повторного исполнения
        ack.duplicate = true;
        duplicate = true;
        break;
      }
    }
  }

  if (duplicate) {
    ++cmd_duplicate_;
  } else {
    ack = Execute(now_ms, cmd, fb);
    if (ack.reason != RejectReason::kInvalidId) {
      seen_[seen_total_ % kSeenCapacity] = ack;
      ++seen_total_;
    }
    if (ack.result == CommandResult::kRejected) ++cmd_rejected_;
  }

  RecordAck(ack);
  Log({.cmd_id = cmd.cmd_id,
       .kind = JournalKind::kCommand,
       .arg0 = static_cast<uint8_t>(cmd.type),
       .arg1 = static_cast<uint8_t>(ack.result),
       .arg2 = static_cast<uint8_t>(ack.reason),
       .flags = static_cast<uint8_t>(duplicate ? 1 : 0)},
      now_ms);
}

CommandAck BenchControlLoop::Execute(uint32_t now_ms, const BenchCommand& cmd,
                                     const ValveFeedback& fb) {
  CommandAck ack{.cmd_id = cmd.cmd_id, .type = cmd.type};
  const auto reject = [&ack](RejectReason reason) {
    ack.result = CommandResult::kRejected;
    ack.reason = reason;
    return ack;
  };

  if (cmd.cmd_id == 0) return reject(RejectReason::kInvalidId);

  switch (cmd.type) {
    case CommandType::kControlledStop:
      // Останавливает и программу; повторный stop в удержании — no-op.
      program_ = SineProgram{};
      program_was_active_ = false;
      EnterHold(now_ms, HoldReason::kOperator,
                StopReason::kOperatorControlledStop, fb);
      return ack;

    case CommandType::kEStopSoft:
      // Программное снятие enable. Не аварийный останов: аппаратная цепь
      // вне MCU (P-2).
      program_ = SineProgram{};
      program_was_active_ = false;
      controller_.SetEnabled(false);
      EnterHold(now_ms, HoldReason::kOperator, StopReason::kOperatorEStopSoft,
                fb);
      return ack;

    case CommandType::kEnable:
      if (!cmd.enable) {
        controller_.SetEnabled(false);
        return ack;
      }
      if (holding_ && hold_reason_ != HoldReason::kOperator) {
        return reject(RejectReason::kHeld);
      }
      if (!program_.Finished()) return reject(RejectReason::kProgramRunning);
      if (holding_) {
        // Возобновление после операторского останова: удерживаем
        // текущую позицию уставкой.
        holding_ = false;
        hold_reason_ = HoldReason::kNone;
        manual_commanded_ = hold_position_mm_;
        manual_ramped_ = hold_position_mm_;
      }
      controller_.SetEnabled(true);
      return ack;

    case CommandType::kSetMode:
    case CommandType::kSetSetpoint:
      if (holding_) {
        return reject(hold_reason_ == HoldReason::kOperator
                          ? RejectReason::kStopped
                          : RejectReason::kHeld);
      }
      if (!program_.Finished()) return reject(RejectReason::kProgramRunning);
      break;
  }

  if (cmd.type == CommandType::kSetMode) {
    const float capture =
        cmd.mode == ControlMode::kForce ? fb.force_n : fb.position_mm;
    if (cmd.mode != controller_.Mode()) {
      controller_.RequestMode(cmd.mode, capture);
      manual_commanded_ = capture;
      manual_ramped_ = capture;
    }
    return ack;
  }

  // kSetSetpoint: лимиты по активному режиму; рампа применяется в тике
  const float limit = controller_.Mode() == ControlMode::kForce
                          ? config_.manual.force_limit_n
                          : config_.manual.position_limit_mm;
  if (!std::isfinite(cmd.value) || std::fabs(cmd.value) > limit) {
    return reject(RejectReason::kOutOfRange);
  }
  manual_commanded_ = cmd.value;
  return ack;
}

void BenchControlLoop::Publish(uint32_t now_ms, const TickSnapshot& tick) {
  ++tick_count_;
  if (config_.stats_refresh_ticks != 0 &&
      tick_count_ % config_.stats_refresh_ticks == 0) {
    cached_stats_ = stats_.MakeReport();
  }
  if (publisher_ == nullptr || config_.publish_every_ticks == 0 ||
      tick_count_ % config_.publish_every_ticks != 0) {
    return;
  }

  BenchStateSnapshot s{};
  s.seq = publish_seq_++;
  s.uptime_ms = now_ms;
  s.mode = tick.mode;
  s.link_state = tick.link_state;
  s.enabled = controller_.Enabled();
  s.holding = holding_;
  s.last_stop_reason = last_stop_reason_;
  s.program_active = !program_.Finished();
  s.failure_latched = tick.failure_latched;
  s.estop_chain = platform_.EstopChain();
  s.program_target = tick.program_target;
  s.manual_commanded = manual_commanded_;
  s.manual_ramped = manual_ramped_;
  s.effective_target = tick.effective_target;
  s.valve_command = tick.valve_command;
  s.force_n = tick.force_n;
  s.position_mm = tick.position_mm;
  s.valve_status = tick.valve_status;
  s.fb_fresh = tick.fb_fresh;
  s.fb_age_ticks = tick.fb_age_ticks;
  s.cmd_total = cmd_total_;
  s.cmd_rejected = cmd_rejected_;
  s.cmd_duplicate = cmd_duplicate_;
  s.cmd_overwritten = inbox_ != nullptr ? inbox_->OverwrittenCount() : 0;

  const uint32_t ack_n =
      ack_total_ < BenchStateSnapshot::kAckTail
          ? ack_total_
          : static_cast<uint32_t>(BenchStateSnapshot::kAckTail);
  for (uint32_t i = 0; i < ack_n; ++i) {
    s.acks[i] = acks_[(ack_total_ - ack_n + i) % BenchStateSnapshot::kAckTail];
  }
  s.ack_count = static_cast<uint8_t>(ack_n);

  s.tick = cached_stats_;
  s.journal_count = static_cast<uint8_t>(
      journal_.CopyTail(s.journal, BenchStateSnapshot::kJournalTail));
  s.journal_total = journal_.Total();

  publisher_->Publish(s);
}

}  // namespace bench
