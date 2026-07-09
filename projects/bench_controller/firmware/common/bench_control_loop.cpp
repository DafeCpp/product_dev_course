#include "bench_control_loop.hpp"

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
  controller_.RequestMode(ControlMode::kForce,
                          valve_.ReadFeedback().force_n);
}

std::expected<void, PlatformError> BenchControlLoop::Init() {
  last_loop_ms_ = platform_.GetTimeMs();
  return platform_.CreateControlTask(&BenchControlLoop::ControlTaskEntry,
                                     this);
}

void BenchControlLoop::ControlTaskEntry(void* arg) {
  static_cast<BenchControlLoop*>(arg)->ControlTaskLoop();
}

void BenchControlLoop::ControlTaskLoop() {
  while (true) {
    platform_.DelayUntilNextTick(config_.period_ms);
    const uint32_t now = platform_.GetTimeMs();
    TickOnce(now, now - last_loop_ms_);
    last_loop_ms_ = now;
    platform_.FeedTaskWdt();
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

  // --- Программа ---
  float program_target = program_.Step(dt_sec);

  // --- Safety: связь с платформой ---
  const LinkState link_state =
      link_watchdog_.Update(now_ms, supervisory_.IsAlive());
  if (link_state == LinkState::kRampDown) {
    // Плавная разгрузка: амплитудная часть стягивается к среднему.
    const float mean = program_.CurrentMean();
    program_target =
        mean + (program_target - mean) * link_watchdog_.RampScale(now_ms);
  } else if (link_state == LinkState::kSafeHold && !holding_) {
    holding_ = true;
    hold_position_mm_ = fb.position_mm;
    controller_.RequestMode(ControlMode::kDisplacement, fb.position_mm);
  }

  // --- Safety: разрушение образца (только в force-режиме) ---
  // Сравниваем с эффективной уставкой прошлого тика (после рампы
  // захвата), а не с целью программы: на стартовой рампе фактическая
  // сила ещё далека от полной программы — это не разрушение.
  if (controller_.Mode() == ControlMode::kForce &&
      failure_detector_.Update(fb.force_n, controller_.EffectiveTarget()) &&
      !holding_) {
    holding_ = true;
    hold_position_mm_ = fb.position_mm;
    controller_.RequestMode(ControlMode::kDisplacement, fb.position_mm);
  }

  // --- Регулятор ---
  const float target = holding_ ? hold_position_mm_ : program_target;
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
      .failure_latched = failure_detector_.Latched(),
  };
}

}  // namespace bench
