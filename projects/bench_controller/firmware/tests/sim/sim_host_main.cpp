// sim_host — ядро прошивки bench_controller как CLI (SIL).
//
// PR-A: режим --fake-plant — замкнутый контур против встроенной
// гидравлической модели, CSV на stdout. PR-B добавит --socketcan.
//
// Пример:
//   sim_host --duration-s 3 --freq 10 --amplitude 10000 --mean 20000 \
//            --fail-at-ms 2000 --link-loss-at-ms 0 > run.csv

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "bench_control_loop.hpp"
#include "fixtures/plant_valve_channel.hpp"
#include "sim/host_platform.hpp"

namespace {

struct Args {
  float duration_s{3.0f};
  float freq_hz{10.0f};
  float amplitude_n{10'000.0f};
  float mean_n{20'000.0f};
  uint32_t fail_at_ms{0};       // 0 = разрушения нет
  uint32_t link_loss_at_ms{0};  // 0 = связь не теряется
};

Args ParseArgs(int argc, char** argv) {
  Args a{};
  for (int i = 1; i < argc - 1; ++i) {
    const std::string key = argv[i];
    const char* val = argv[i + 1];
    if (key == "--duration-s")
      a.duration_s = std::strtof(val, nullptr);
    else if (key == "--freq")
      a.freq_hz = std::strtof(val, nullptr);
    else if (key == "--amplitude")
      a.amplitude_n = std::strtof(val, nullptr);
    else if (key == "--mean")
      a.mean_n = std::strtof(val, nullptr);
    else if (key == "--fail-at-ms")
      a.fail_at_ms = std::strtoul(val, nullptr, 10);
    else if (key == "--link-loss-at-ms")
      a.link_loss_at_ms = std::strtoul(val, nullptr, 10);
  }
  return a;
}

const char* ModeName(bench::ControlMode m) {
  return m == bench::ControlMode::kForce ? "force" : "disp";
}

const char* LinkName(bench::LinkState s) {
  switch (s) {
    case bench::LinkState::kRunning:
      return "running";
    case bench::LinkState::kGracePeriod:
      return "grace";
    case bench::LinkState::kRampDown:
      return "ramp";
    case bench::LinkState::kSafeHold:
      return "hold";
  }
  return "?";
}

}  // namespace

int main(int argc, char** argv) {
  const Args args = ParseArgs(argc, argv);

  bench::HostPlatform platform;
  bench::testing::PlantValveChannel valve(bench::HydraulicPlantModel::Config{});
  bench::StubSupervisoryLink link;

  bench::BenchControlLoop::Config cfg{};
  cfg.controller.force_gains = {.kp = 4e-5f,
                                .ki = 2e-3f,
                                .kd = 0.0f,
                                .max_integral = 400.0f,
                                .max_output = 1.0f};
  cfg.controller.disp_gains = {.kp = 0.3f,
                               .ki = 4.0f,
                               .kd = 0.0f,
                               .max_integral = 0.2f,
                               .max_output = 1.0f};
  cfg.controller.force_ff = 1.0f / (5000.0f * 400.0f);
  cfg.controller.disp_ff = 1.0f / 400.0f;
  cfg.controller.ff_lead_tau_s = 0.008f;
  cfg.controller.output_slew_per_s = 400.0f;
  cfg.controller.capture_ramp_s = 0.3f;
  cfg.watchdog = {.grace_ms = 200, .ramp_ms = 1000};

  bench::BenchControlLoop loop(cfg, platform, valve, link);
  const bench::SineProgram::Segment segments[] = {
      {.mean = args.mean_n,
       .amplitude = args.amplitude_n,
       .freq_hz = args.freq_hz,
       .cycles = 1'000'000}};
  loop.SetProgram(segments);

  const uint32_t total_ticks =
      static_cast<uint32_t>(args.duration_s * 1000.0f) / cfg.period_ms;

  std::printf(
      "now_ms,mode,link,program_target,effective_target,valve_cmd,"
      "force_n,position_mm,failure\n");

  for (uint32_t i = 0; i < total_ticks; ++i) {
    platform.AdvanceTimeMs(cfg.period_ms);
    const uint32_t now = platform.GetTimeMs();
    if (args.fail_at_ms != 0 && now >= args.fail_at_ms &&
        !valve.Plant().Failed()) {
      valve.Plant().TriggerFailure();
    }
    if (args.link_loss_at_ms != 0 && now >= args.link_loss_at_ms) {
      link.SetAlive(false);
    }
    const bench::TickSnapshot s = loop.HostStep(cfg.period_ms);
    std::printf("%u,%s,%s,%.1f,%.1f,%.4f,%.1f,%.3f,%d\n", s.now_ms,
                ModeName(s.mode), LinkName(s.link_state), s.program_target,
                s.effective_target, s.valve_command, s.force_n, s.position_mm,
                s.failure_latched ? 1 : 0);
  }
  return 0;
}
