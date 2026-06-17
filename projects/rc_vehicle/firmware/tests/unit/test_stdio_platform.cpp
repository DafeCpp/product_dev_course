#include <gtest/gtest.h>

#include <cmath>
#include <memory>

#include "imu_sensor.hpp"
#include "stdio_platform.hpp"
#include "vehicle_control_unified.hpp"

using ::MagData;
using rc_vehicle::RcCommand;
using rc_vehicle::VehicleControlUnified;
using rc_vehicle::sim::FormatOutputLine;
using rc_vehicle::sim::InputFrame;
using rc_vehicle::sim::OutputHeader;
using rc_vehicle::sim::ParseInputLine;
using rc_vehicle::sim::StdioPlatform;

namespace {

int CountCommas(const std::string& s) {
  int n = 0;
  for (char c : s) n += (c == ',');
  return n;
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Протокол: парсинг входа / формат выхода
// ═══════════════════════════════════════════════════════════════════════════

TEST(SimProtocol, ParseInputLine_ParsesAllFields) {
  InputFrame f;
  const bool ok = ParseInputLine(
      "2,0.1,0.2,0.98,1.5,-2.5,3.5,1,10,20,30,1,0.4,-0.3,0,0,0", f);
  ASSERT_TRUE(ok);
  EXPECT_EQ(f.dt_ms, 2u);
  EXPECT_FLOAT_EQ(f.imu.ax, 0.1f);
  EXPECT_FLOAT_EQ(f.imu.az, 0.98f);
  EXPECT_FLOAT_EQ(f.imu.gy, -2.5f);
  EXPECT_TRUE(f.mag_present);
  EXPECT_FLOAT_EQ(f.mag.mz, 30.0f);
  EXPECT_TRUE(f.rc_present);
  EXPECT_FLOAT_EQ(f.rc.throttle, 0.4f);
  EXPECT_FLOAT_EQ(f.rc.steering, -0.3f);
  EXPECT_FALSE(f.wifi_present);
}

TEST(SimProtocol, ParseInputLine_RejectsTooFewFields) {
  InputFrame f;
  EXPECT_FALSE(ParseInputLine("2,0.1,0.2", f));
}

TEST(SimProtocol, ParseInputLine_IgnoresTrailingCR) {
  InputFrame f;
  ASSERT_TRUE(ParseInputLine("4,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0\r", f));
  EXPECT_EQ(f.dt_ms, 4u);
  EXPECT_FLOAT_EQ(f.imu.az, 1.0f);
}

TEST(SimProtocol, OutputHeaderArityMatchesFormattedLine) {
  rc_vehicle::TelemetrySnapshot snap{};
  const std::string row = FormatOutputLine(snap, 0.0f, 0.0f, false);
  EXPECT_EQ(CountCommas(OutputHeader()), CountCommas(row));
}

// ═══════════════════════════════════════════════════════════════════════════
// sim_host driver: настоящий ControlLoopProcessor через HostStep
// ═══════════════════════════════════════════════════════════════════════════

class SimHostTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto platform = std::make_unique<StdioPlatform>();
    p_ = platform.get();
    p_->SetIdentityCalib(true);  // replay «со средней точки»
    u_.SetPlatform(std::move(platform));
    ASSERT_EQ(u_.Init(), rc_vehicle::PlatformError::Ok);
  }

  void Drive(const InputFrame& f) {
    p_->SetImuData(f.imu);
    p_->SetMag(f.mag_present ? std::optional<MagData>{f.mag} : std::nullopt);
    p_->SetRc(f.rc_present ? std::optional<RcCommand>{f.rc} : std::nullopt);
    p_->SetWifi(f.wifi_present ? std::optional<RcCommand>{f.wifi}
                               : std::nullopt);
    p_->AdvanceTimeMs(f.dt_ms);
    u_.HostStep(f.dt_ms);
  }

  // Кадр с гравитацией по Z и нулевым гиро (машинка стоит ровно).
  static InputFrame LevelFrame() {
    InputFrame f;
    f.dt_ms = 2;
    f.imu.az = 1.0f;  // 1 g вниз
    return f;
  }

  VehicleControlUnified u_;
  StdioPlatform* p_{nullptr};
};

TEST_F(SimHostTest, NoCommands_FailsafeNeutral) {
  for (int i = 0; i < 50; ++i) Drive(LevelFrame());  // нет RC, нет Wi-Fi
  EXPECT_TRUE(p_->WasNeutral());
  EXPECT_FLOAT_EQ(p_->GetLastThrottle(), 0.0f);
  EXPECT_FLOAT_EQ(p_->GetLastSteering(), 0.0f);
  EXPECT_TRUE(p_->GetLastSnap().failsafe);
}

TEST_F(SimHostTest, ManySteps_OutputsFiniteAndInRange) {
  for (int i = 0; i < 200; ++i) {
    InputFrame f = LevelFrame();
    f.rc_present = true;
    f.rc.throttle = 0.3f;
    f.rc.steering = 0.2f;
    f.imu.gz = 5.0f;  // небольшой поворот
    Drive(f);

    const float thr = p_->GetLastThrottle();
    const float str = p_->GetLastSteering();
    ASSERT_TRUE(std::isfinite(thr));
    ASSERT_TRUE(std::isfinite(str));
    EXPECT_GE(thr, -1.0f);
    EXPECT_LE(thr, 1.0f);
    EXPECT_GE(str, -1.0f);
    EXPECT_LE(str, 1.0f);
  }
  const auto& s = p_->GetLastSnap();
  EXPECT_TRUE(std::isfinite(s.yaw_deg));
  EXPECT_TRUE(std::isfinite(s.ekf_vx));
  EXPECT_FALSE(s.failsafe);  // RC активен → нет failsafe
}

TEST_F(SimHostTest, FormatOutputLine_NoNaNFromRealSnapshot) {
  for (int i = 0; i < 10; ++i) {
    InputFrame f = LevelFrame();
    f.rc_present = true;
    f.rc.throttle = 0.5f;
    Drive(f);
  }
  const std::string row =
      FormatOutputLine(p_->GetLastSnap(), p_->GetLastThrottle(),
                       p_->GetLastSteering(), p_->WasNeutral());
  EXPECT_EQ(row.find("nan"), std::string::npos);
  EXPECT_EQ(row.find("inf"), std::string::npos);
}
