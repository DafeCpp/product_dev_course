// sim_host — host-исполняемый файл прошивки для SIL-симуляции (FW-S2.1).
//
// Гоняет НАСТОЯЩИЙ ControlLoopProcessor (через VehicleControlUnified::Init() →
// HostStep): калибровка → Madgwick → EKF → стабилизация → PWM. Сенсоры/команды
// читаются построчно из stdin, выход (PWM + телеметрия) пишется в stdout.
//
// Режимы:
//   --interactive (по умолчанию) — кадр-за-кадром, flush после каждого
//       (для closed-loop SIL: Python зависит от выхода тика для следующего
//       входа).
//   --batch — прочитать весь stdin, затем вывести всё (без дедлока pipe;
//   replay).
//   --identity-calib — заменить калибровку на identity (replay «со средней
//   точки»).
//   --oversteer [--oversteer-slip-thresh deg]
//       [--oversteer-rate-thresh deg/s] [--oversteer-throttle-reduction 0..1]
//       — воспроизвести oversteer-конфиг, записанный в provenance эпизода.
//   --start-test <straight|circle|step> [--target-accel g]
//       [--test-duration s] [--test-steering v] — запустить авто-манёвр
//       (аналог start_test по WebSocket) до первого кадра; машина едет сама,
//       RC-команды в сценарии не нужны.
//
// Время логическое: реальных пауз нет, dt берётся из кадра.

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "stdio_platform.hpp"
#include "vehicle_control_unified.hpp"

namespace {

using rc_vehicle::VehicleControlUnified;
using rc_vehicle::sim::FormatOutputLine;
using rc_vehicle::sim::InputFrame;
using rc_vehicle::sim::OutputHeader;
using rc_vehicle::sim::ParseInputLine;
using rc_vehicle::sim::StdioPlatform;

void ApplyFrame(StdioPlatform& p, const InputFrame& f) {
  p.SetImuData(f.imu);
  p.SetMag(f.mag_present ? std::optional<MagData>{f.mag} : std::nullopt);
  p.SetRc(f.rc_present ? std::optional<rc_vehicle::RcCommand>{f.rc}
                       : std::nullopt);
  p.SetWifi(f.wifi_present ? std::optional<rc_vehicle::RcCommand>{f.wifi}
                           : std::nullopt);
  p.AdvanceTimeMs(f.dt_ms);
}

std::string StepAndFormat(VehicleControlUnified& u, StdioPlatform& p,
                          const InputFrame& f) {
  ApplyFrame(p, f);
  u.HostStep(f.dt_ms);
  return FormatOutputLine(p.GetLastSnap(), p.GetLastThrottle(),
                          p.GetLastSteering(), p.WasNeutral(),
                          u.IsTestActive());
}

}  // namespace

rc_vehicle::TestType ParseTestType(std::string_view s) {
  if (s == "circle") return rc_vehicle::TestType::Circle;
  if (s == "step") return rc_vehicle::TestType::Step;
  return rc_vehicle::TestType::Straight;
}

rc_vehicle::DriveMode ParseDriveMode(std::string_view s) {
  if (s == "kids") return rc_vehicle::DriveMode::Kids;
  if (s == "sport") return rc_vehicle::DriveMode::Sport;
  if (s == "drift") return rc_vehicle::DriveMode::Drift;
  if (s == "directlaw") return rc_vehicle::DriveMode::DirectLaw;
  return rc_vehicle::DriveMode::Normal;
}

int main(int argc, char** argv) {
  bool batch = false;
  bool identity_calib = false;
  rc_vehicle::DriveMode drive_mode = rc_vehicle::DriveMode::Normal;
  float speed_limit = 0.0f;
  bool stabilize = false;
  bool oversteer = false;
  float oversteer_slip_thresh_deg = 10.0f;
  float oversteer_rate_thresh_deg_s = 30.0f;
  float oversteer_throttle_reduction = 0.7f;
  bool start_test = false;
  rc_vehicle::TestParams test_params;
  for (int i = 1; i < argc; ++i) {
    const std::string_view a = argv[i];
    if (a == "--batch")
      batch = true;
    else if (a == "--interactive")
      batch = false;
    else if (a == "--identity-calib")
      identity_calib = true;
    else if (a == "--drive-mode" && i + 1 < argc)
      drive_mode = ParseDriveMode(argv[++i]);
    else if (a == "--speed-limit" && i + 1 < argc)
      speed_limit = std::strtof(argv[++i], nullptr);
    else if (a == "--stabilize")
      stabilize = true;
    else if (a == "--oversteer")
      oversteer = true;
    else if (a == "--oversteer-slip-thresh" && i + 1 < argc) {
      oversteer = true;
      oversteer_slip_thresh_deg = std::strtof(argv[++i], nullptr);
    } else if (a == "--oversteer-rate-thresh" && i + 1 < argc) {
      oversteer = true;
      oversteer_rate_thresh_deg_s = std::strtof(argv[++i], nullptr);
    } else if (a == "--oversteer-throttle-reduction" && i + 1 < argc) {
      oversteer = true;
      oversteer_throttle_reduction = std::strtof(argv[++i], nullptr);
    } else if (a == "--start-test" && i + 1 < argc) {
      start_test = true;
      test_params.type = ParseTestType(argv[++i]);
    } else if (a == "--target-accel" && i + 1 < argc)
      test_params.target_accel_g = std::strtof(argv[++i], nullptr);
    else if (a == "--test-duration" && i + 1 < argc)
      test_params.duration_sec = std::strtof(argv[++i], nullptr);
    else if (a == "--test-steering" && i + 1 < argc)
      test_params.steering = std::strtof(argv[++i], nullptr);
  }

  auto platform = std::make_unique<StdioPlatform>();
  StdioPlatform* p = platform.get();
  p->SetIdentityCalib(identity_calib);
  p->SetDriveMode(drive_mode);
  p->SetSpeedLimit(speed_limit);
  p->SetStabilize(stabilize);
  if (oversteer) {
    rc_vehicle::OversteerConfig cfg;
    cfg.warn_enabled = true;
    cfg.slip_thresh_deg = oversteer_slip_thresh_deg;
    cfg.rate_thresh_deg_s = oversteer_rate_thresh_deg_s;
    cfg.throttle_reduction = oversteer_throttle_reduction;
    cfg.Clamp();
    p->SetOversteerConfig(cfg);
  }

  VehicleControlUnified unified;
  unified.SetPlatform(std::move(platform));
  if (unified.Init() != rc_vehicle::PlatformError::Ok) {
    std::cerr << "sim_host: Init() failed\n";
    return 1;
  }

  // Авто-тест стартует до первого кадра: пульт в SIL неактивен, поэтому
  // процедура получает тики сразу (аналог start_test по WebSocket).
  if (start_test && !unified.StartTest(test_params)) {
    std::cerr << "sim_host: StartTest() rejected\n";
    return 1;
  }

  std::cout << OutputHeader() << '\n';

  std::string line;
  if (batch) {
    std::vector<InputFrame> frames;
    while (std::getline(std::cin, line)) {
      InputFrame f;
      if (ParseInputLine(line, f)) frames.push_back(f);
    }
    std::string out;
    for (const InputFrame& f : frames) {
      out += StepAndFormat(unified, *p, f);
      out += '\n';
    }
    std::cout << out;
    std::cout.flush();
  } else {
    std::cout.flush();
    while (std::getline(std::cin, line)) {
      InputFrame f;
      if (!ParseInputLine(line, f)) continue;
      std::cout << StepAndFormat(unified, *p, f) << '\n';
      std::cout.flush();
    }
  }
  return 0;
}
