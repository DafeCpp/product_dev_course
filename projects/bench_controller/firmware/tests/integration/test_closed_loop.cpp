// Интеграционные сценарии замкнутого контура: ядро + гидравлическая
// модель через PlantValveChannel, логическое время HostPlatform.
// Без CAN и Python — гоняется в ctest (сценарии (a)–(d) плана LOS-76).

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "bench_control_loop.hpp"
#include "fixtures/plant_valve_channel.hpp"
#include "sim/host_platform.hpp"

namespace bench {
namespace {

constexpr uint32_t kPeriodMs = 2;
constexpr float kDtSec = 0.002f;

struct Rig {
  HostPlatform platform{};
  testing::PlantValveChannel valve{HydraulicPlantModel::Config{}};
  StubSupervisoryLink link{};
  BenchControlLoop loop;

  explicit Rig(const BenchControlLoop::Config& cfg)
      : loop(cfg, platform, valve, link) {}

  TickSnapshot Tick() {
    platform.AdvanceTimeMs(kPeriodMs);
    return loop.HostStep(kPeriodMs);
  }
};

BenchControlLoop::Config MakeConfig() {
  BenchControlLoop::Config cfg{};
  cfg.period_ms = kPeriodMs;
  cfg.controller.force_gains = {.kp = 4e-5f, .ki = 2e-3f, .kd = 0.0f,
                                .max_integral = 400.0f, .max_output = 1.0f};
  cfg.controller.disp_gains = {.kp = 0.3f, .ki = 4.0f, .kd = 0.0f,
                               .max_integral = 0.2f, .max_output = 1.0f};
  // FF = 1/K_plant: сила — 1/(жёсткость·скорость поршня), позиция —
  // 1/скорость поршня (см. HydraulicPlantModel::Config).
  cfg.controller.force_ff = 1.0f / (5000.0f * 400.0f);
  cfg.controller.disp_ff = 1.0f / 400.0f;
  cfg.controller.ff_lead_tau_s = 0.008f;  // компенсация лага золотника
  cfg.controller.output_slew_per_s = 400.0f;
  cfg.controller.capture_ramp_s = 0.3f;
  cfg.failure = {.drop_fraction = 0.3f, .window_ticks = 10,
                 .min_force_n = 1000.0f, .arm_ticks = 25};
  cfg.watchdog = {.grace_ms = 200, .ramp_ms = 1000};
  return cfg;
}

// Прогнать duration_s и вернуть срезы (после стартовой рампы захвата).
std::vector<TickSnapshot> RunFor(Rig& rig, float duration_s) {
  std::vector<TickSnapshot> out;
  const auto ticks = static_cast<uint32_t>(duration_s / kDtSec);
  out.reserve(ticks);
  for (uint32_t i = 0; i < ticks; ++i) out.push_back(rig.Tick());
  return out;
}

// (a) Слежение за синусом в force-режиме.
void ExpectSineTracking(float freq_hz, float amplitude_n,
                        float rms_limit_frac, float max_limit_frac) {
  Rig rig(MakeConfig());
  const SineProgram::Segment seg[] = {{.mean = 20'000.0f,
                                       .amplitude = amplitude_n,
                                       .freq_hz = freq_hz,
                                       .cycles = 1'000'000}};
  rig.loop.SetProgram(seg);

  (void)RunFor(rig, 1.0f);  // стартовая рампа захвата + выход на режим
  const auto snaps = RunFor(rig, 2.0f);

  double sq_sum = 0.0;
  float max_err = 0.0f;
  for (const auto& s : snaps) {
    const float err = s.effective_target - s.force_n;
    sq_sum += static_cast<double>(err) * err;
    max_err = std::max(max_err, std::fabs(err));
  }
  const float rms = static_cast<float>(
      std::sqrt(sq_sum / static_cast<double>(snaps.size())));

  EXPECT_LT(rms, amplitude_n * rms_limit_frac)
      << freq_hz << " Гц: RMS " << rms;
  EXPECT_LT(max_err, amplitude_n * max_limit_frac)
      << freq_hz << " Гц: max " << max_err;
}

TEST(ClosedLoop, ForceSineTracking10Hz) {
  // 10 Гц — основной режим процесса: ошибка в единицы процентов.
  ExpectSineTracking(10.0f, 10'000.0f, 0.06f, 0.12f);
}

TEST(ClosedLoop, ForceSineTracking20Hz) {
  // 20 Гц — середина запасного диапазона (замер: RMS ≈ 23 %).
  ExpectSineTracking(20.0f, 5'000.0f, 0.28f, 0.5f);
}

TEST(ClosedLoop, ForceSineTracking50Hz) {
  // 50 Гц — граница применимости 500 Гц контура: 10 отсчётов/цикл,
  // полутиковые задержки дискретных производных дают ~50 % амплитудной
  // ошибки даже с lead-FF. Тест фиксирует эту границу (вход в отчёт
  // LOS-76: для 50 Гц нужен предиктивный FF по известной программе
  // либо более высокая частота контура).
  ExpectSineTracking(50.0f, 2'000.0f, 0.55f, 0.85f);
}

// (b) Безударность старта force-программы (переключение disp → force).
TEST(ClosedLoop, StartupCaptureIsBumpless) {
  Rig rig(MakeConfig());
  const SineProgram::Segment seg[] = {{.mean = 15'000.0f,
                                       .amplitude = 5'000.0f,
                                       .freq_hz = 10.0f,
                                       .cycles = 1'000'000}};
  rig.loop.SetProgram(seg);  // внутри — RequestMode(kForce, captured)

  const float max_step =
      MakeConfig().controller.output_slew_per_s * kDtSec * 1.001f;
  float prev = 0.0f;
  for (int i = 0; i < 1000; ++i) {
    const auto s = rig.Tick();
    EXPECT_LE(std::fabs(s.valve_command - prev), max_step) << "tick " << i;
    prev = s.valve_command;
  }
}

// (c) Разрушение образца → displacement-hold за ≤ 2 цикла процесса.
TEST(ClosedLoop, SpecimenFailureSwitchesToDisplacementHold) {
  Rig rig(MakeConfig());
  const SineProgram::Segment seg[] = {{.mean = 20'000.0f,
                                       .amplitude = 10'000.0f,
                                       .freq_hz = 10.0f,
                                       .cycles = 1'000'000}};
  rig.loop.SetProgram(seg);
  (void)RunFor(rig, 1.5f);  // выход на режим

  rig.valve.Plant().TriggerFailure();
  const float pos_at_failure = rig.valve.Plant().GetState().position_mm;

  // 2 цикла по 100 мс = 100 тиков.
  uint32_t ticks_to_latch = 0;
  TickSnapshot s{};
  for (; ticks_to_latch < 100; ++ticks_to_latch) {
    s = rig.Tick();
    if (s.failure_latched) break;
  }
  ASSERT_TRUE(s.failure_latched) << "детекция не сработала за 2 цикла";
  EXPECT_EQ(s.mode, ControlMode::kDisplacement);
  const float pos_at_latch = s.position_mm;

  // За окно детекции force-регулятор «догоняет» упавшую силу и цилиндр
  // убегает — runaway неустраним, но должен быть ограничен.
  EXPECT_LT(std::fabs(pos_at_latch - pos_at_failure), 10.0f)
      << "runaway за окно детекции: " << pos_at_latch - pos_at_failure;

  // Удержание: после латча позиция стабилизируется у точки захвата.
  const auto after = RunFor(rig, 1.0f);
  EXPECT_EQ(after.back().mode, ControlMode::kDisplacement);
  EXPECT_NEAR(after.back().position_mm, pos_at_latch, 2.0f);
}

// (d) Потеря связи → grace → плавная разгрузка → SafeHold.
TEST(ClosedLoop, LinkLossRampsDownSmoothlyToSafeHold) {
  auto cfg = MakeConfig();
  Rig rig(cfg);
  const SineProgram::Segment seg[] = {{.mean = 15'000.0f,
                                       .amplitude = 8'000.0f,
                                       .freq_hz = 10.0f,
                                       .cycles = 1'000'000}};
  rig.loop.SetProgram(seg);
  (void)RunFor(rig, 1.5f);

  rig.link.SetAlive(false);

  const float max_step =
      cfg.controller.output_slew_per_s * kDtSec * 1.001f;
  float prev_cmd = rig.loop.LastSnapshot().valve_command;
  bool seen_ramp = false;
  TickSnapshot s{};
  // grace 200 мс + ramp 1000 мс + запас.
  for (int i = 0; i < 900; ++i) {
    s = rig.Tick();
    EXPECT_LE(std::fabs(s.valve_command - prev_cmd), max_step)
        << "tick " << i;
    prev_cmd = s.valve_command;
    if (s.link_state == LinkState::kRampDown) seen_ramp = true;
  }
  EXPECT_TRUE(seen_ramp);
  EXPECT_EQ(s.link_state, LinkState::kSafeHold);
  EXPECT_EQ(s.mode, ControlMode::kDisplacement);

  // В SafeHold цель — фиксация позиции; программа не исполняется.
  const float held_pos = s.position_mm;
  const auto after = RunFor(rig, 0.5f);
  EXPECT_NEAR(after.back().position_mm, held_pos, 1.0f);
}

}  // namespace
}  // namespace bench
