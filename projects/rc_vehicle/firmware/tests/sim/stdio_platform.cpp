#include "stdio_platform.hpp"

#include <cstdlib>
#include <sstream>
#include <vector>

namespace rc_vehicle {
namespace sim {

namespace {

/** Разбить строку по запятым в вектор токенов. */
std::vector<std::string> Split(std::string_view line) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : line) {
    if (c == ',') {
      out.push_back(cur);
      cur.clear();
    } else if (c != '\r' && c != '\n') {
      cur.push_back(c);
    }
  }
  out.push_back(cur);
  return out;
}

float ToF(const std::string& s) { return std::strtof(s.c_str(), nullptr); }
bool ToB(const std::string& s) {
  return std::strtol(s.c_str(), nullptr, 10) != 0;
}

}  // namespace

bool ParseInputLine(std::string_view line, InputFrame& out) {
  const std::vector<std::string> t = Split(line);
  if (t.size() < 17) return false;

  out.dt_ms = static_cast<uint32_t>(std::strtoul(t[0].c_str(), nullptr, 10));
  out.imu.ax = ToF(t[1]);
  out.imu.ay = ToF(t[2]);
  out.imu.az = ToF(t[3]);
  out.imu.gx = ToF(t[4]);
  out.imu.gy = ToF(t[5]);
  out.imu.gz = ToF(t[6]);
  out.mag_present = ToB(t[7]);
  out.mag.mx = ToF(t[8]);
  out.mag.my = ToF(t[9]);
  out.mag.mz = ToF(t[10]);
  out.rc_present = ToB(t[11]);
  out.rc.throttle = ToF(t[12]);
  out.rc.steering = ToF(t[13]);
  out.wifi_present = ToB(t[14]);
  out.wifi.throttle = ToF(t[15]);
  out.wifi.steering = ToF(t[16]);
  return true;
}

std::string OutputHeader() {
  return "throttle,steering,neutral,failsafe,rc_throttle,rc_steering,"
         "cmd_throttle,cmd_steering,yaw_deg,pitch_deg,roll_deg,filtered_gz,"
         "heading_deg,heading_rel_deg,ekf_vx,ekf_vy,ekf_yaw_rate,ekf_slip_deg,"
         "ekf_speed_ms,ekf_vx_var,ekf_vy_var,ekf_r_var,oversteer_active,"
         "kids_mode_active,kids_throttle_limit,forward_accel,test_active";
}

std::string FormatOutputLine(const TelemetrySnapshot& s, float throttle,
                             float steering, bool neutral, bool test_active) {
  std::ostringstream os;
  os.precision(6);
  os << throttle << ',' << steering << ',' << (neutral ? 1 : 0) << ','
     << (s.failsafe ? 1 : 0) << ',' << s.rc_throttle << ',' << s.rc_steering
     << ',' << s.cmd_throttle << ',' << s.cmd_steering << ',' << s.yaw_deg
     << ',' << s.pitch_deg << ',' << s.roll_deg << ',' << s.filtered_gz << ','
     << s.heading_deg << ',' << s.heading_rel_deg << ',' << s.ekf_vx << ','
     << s.ekf_vy << ',' << s.ekf_yaw_rate << ',' << s.ekf_slip_deg << ','
     << s.ekf_speed_ms << ',' << s.ekf_vx_var << ',' << s.ekf_vy_var << ','
     << s.ekf_r_var << ',' << (s.oversteer_active ? 1 : 0) << ','
     << (s.kids_mode_active ? 1 : 0) << ',' << s.kids_throttle_limit << ','
     << s.forward_accel << ',' << (test_active ? 1 : 0);
  return os.str();
}

}  // namespace sim
}  // namespace rc_vehicle
