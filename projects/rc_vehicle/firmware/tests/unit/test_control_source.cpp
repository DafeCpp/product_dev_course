#include <gtest/gtest.h>

#include "control_components.hpp"
#include "imu_calibration.hpp"
#include "madgwick_filter.hpp"
#include "mock_platform.hpp"

using namespace rc_vehicle;
using namespace rc_vehicle::testing;

// ══════════════════════════════════════════════════════════════════════════════
// RcInputHandler
// ══════════════════════════════════════════════════════════════════════════════

class RcInputHandlerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    handler_ = std::make_unique<RcInputHandler>(platform_, 20);  // 20 ms poll
  }

  FakePlatform platform_;
  std::unique_ptr<RcInputHandler> handler_;
};

TEST_F(RcInputHandlerTest, InactiveByDefault) {
  EXPECT_FALSE(handler_->IsActive());
  EXPECT_FALSE(handler_->GetCommand().has_value());
}

TEST_F(RcInputHandlerTest, BecomesActive_WhenRcPresent) {
  platform_.SetRcCommand(RcCommand{0.5f, -0.3f});
  platform_.SetTimeMs(20);
  handler_->Update(20, 2);
  EXPECT_TRUE(handler_->IsActive());
  auto cmd = handler_->GetCommand();
  ASSERT_TRUE(cmd.has_value());
  EXPECT_FLOAT_EQ(cmd->throttle, 0.5f);
  EXPECT_FLOAT_EQ(cmd->steering, -0.3f);
}

TEST_F(RcInputHandlerTest, BecomesInactive_WhenRcLost) {
  // First: RC present
  platform_.SetRcCommand(RcCommand{0.5f, 0.0f});
  platform_.SetTimeMs(20);
  handler_->Update(20, 2);
  EXPECT_TRUE(handler_->IsActive());

  // Then: RC lost
  platform_.ClearRcCommand();
  platform_.SetTimeMs(40);
  handler_->Update(40, 20);
  EXPECT_FALSE(handler_->IsActive());
}

TEST_F(RcInputHandlerTest, RespectsPollingInterval) {
  platform_.SetRcCommand(RcCommand{0.5f, 0.0f});

  // Too early to poll (only 10ms since start)
  platform_.SetTimeMs(10);
  handler_->Update(10, 10);
  EXPECT_FALSE(handler_->IsActive()) << "Should not poll before interval";

  // Now at 20ms — should poll
  platform_.SetTimeMs(20);
  handler_->Update(20, 10);
  EXPECT_TRUE(handler_->IsActive());
}

TEST_F(RcInputHandlerTest, UpdatesCommand_OnEachPoll) {
  platform_.SetRcCommand(RcCommand{0.2f, 0.1f});
  platform_.SetTimeMs(20);
  handler_->Update(20, 20);

  platform_.SetRcCommand(RcCommand{0.8f, -0.5f});
  platform_.SetTimeMs(40);
  handler_->Update(40, 20);

  auto cmd = handler_->GetCommand();
  ASSERT_TRUE(cmd.has_value());
  EXPECT_FLOAT_EQ(cmd->throttle, 0.8f);
  EXPECT_FLOAT_EQ(cmd->steering, -0.5f);
}

// ══════════════════════════════════════════════════════════════════════════════
// WifiCommandHandler
// ══════════════════════════════════════════════════════════════════════════════

class WifiCommandHandlerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    handler_ = std::make_unique<WifiCommandHandler>(platform_, 500);
  }

  FakePlatform platform_;
  std::unique_ptr<WifiCommandHandler> handler_;
};

TEST_F(WifiCommandHandlerTest, InactiveByDefault) {
  EXPECT_FALSE(handler_->IsActive());
  EXPECT_FALSE(handler_->GetCommand().has_value());
}

TEST_F(WifiCommandHandlerTest, BecomesActive_WhenCommandReceived) {
  platform_.SetWifiCommand(RcCommand{0.3f, 0.2f});
  platform_.SetTimeMs(100);
  handler_->Update(100, 2);
  EXPECT_TRUE(handler_->IsActive());
  auto cmd = handler_->GetCommand();
  ASSERT_TRUE(cmd.has_value());
  EXPECT_FLOAT_EQ(cmd->throttle, 0.3f);
  EXPECT_FLOAT_EQ(cmd->steering, 0.2f);
}

TEST_F(WifiCommandHandlerTest, BecomesInactive_AfterTimeout) {
  platform_.SetWifiCommand(RcCommand{0.3f, 0.2f});
  platform_.SetTimeMs(100);
  handler_->Update(100, 2);
  EXPECT_TRUE(handler_->IsActive());

  // Clear queue and advance past timeout (500ms)
  platform_.ClearWifiCommand();
  platform_.SetTimeMs(601);
  handler_->Update(601, 2);
  EXPECT_FALSE(handler_->IsActive());
}

TEST_F(WifiCommandHandlerTest, StaysActive_BeforeTimeout) {
  platform_.SetWifiCommand(RcCommand{0.3f, 0.2f});
  platform_.SetTimeMs(100);
  handler_->Update(100, 2);

  // No new command, but within timeout
  platform_.ClearWifiCommand();
  platform_.SetTimeMs(500);
  handler_->Update(500, 2);
  EXPECT_TRUE(handler_->IsActive()) << "Should stay active within timeout";
}

TEST_F(WifiCommandHandlerTest, UpdatesCommand_OnNewData) {
  platform_.SetWifiCommand(RcCommand{0.1f, 0.0f});
  platform_.SetTimeMs(100);
  handler_->Update(100, 2);

  platform_.SetWifiCommand(RcCommand{0.9f, -1.0f});
  platform_.SetTimeMs(200);
  handler_->Update(200, 2);

  auto cmd = handler_->GetCommand();
  ASSERT_TRUE(cmd.has_value());
  EXPECT_FLOAT_EQ(cmd->throttle, 0.9f);
  EXPECT_FLOAT_EQ(cmd->steering, -1.0f);
}

TEST_F(WifiCommandHandlerTest, RefreshesTimeout_OnNewCommand) {
  platform_.SetWifiCommand(RcCommand{0.3f, 0.0f});
  platform_.SetTimeMs(100);
  handler_->Update(100, 2);

  // Send another command just before timeout
  platform_.SetWifiCommand(RcCommand{0.5f, 0.0f});
  platform_.SetTimeMs(550);
  handler_->Update(550, 2);

  // Should still be active (timeout refreshed at 550)
  platform_.ClearWifiCommand();
  platform_.SetTimeMs(900);
  handler_->Update(900, 2);
  EXPECT_TRUE(handler_->IsActive());

  // Now past new timeout (550 + 500 = 1050)
  platform_.SetTimeMs(1051);
  handler_->Update(1051, 2);
  EXPECT_FALSE(handler_->IsActive());
}

// ══════════════════════════════════════════════════════════════════════════════
// Control Source Priority: RC > WiFi
// ══════════════════════════════════════════════════════════════════════════════

class ControlSourcePriorityTest : public ::testing::Test {
 protected:
  void SetUp() override {
    rc_ = std::make_unique<RcInputHandler>(platform_, 20);
    wifi_ = std::make_unique<WifiCommandHandler>(platform_, 500);
  }

  /// Simulate the control source selection logic from VehicleControlUnified
  bool SelectSource(float& throttle, float& steering) {
    if (rc_->IsActive()) {
      auto cmd = rc_->GetCommand();
      if (cmd) {
        throttle = cmd->throttle;
        steering = cmd->steering;
        return true;
      }
    } else if (wifi_->IsActive()) {
      auto cmd = wifi_->GetCommand();
      if (cmd) {
        throttle = cmd->throttle;
        steering = cmd->steering;
        return true;
      }
    }
    return false;
  }

  FakePlatform platform_;
  std::unique_ptr<RcInputHandler> rc_;
  std::unique_ptr<WifiCommandHandler> wifi_;
};

TEST_F(ControlSourcePriorityTest, NoSource_ReturnsFailse) {
  float t = 0, s = 0;
  EXPECT_FALSE(SelectSource(t, s));
}

TEST_F(ControlSourcePriorityTest, RcOnly) {
  platform_.SetRcCommand(RcCommand{0.5f, 0.3f});
  platform_.SetTimeMs(20);
  rc_->Update(20, 20);

  float t = 0, s = 0;
  EXPECT_TRUE(SelectSource(t, s));
  EXPECT_FLOAT_EQ(t, 0.5f);
  EXPECT_FLOAT_EQ(s, 0.3f);
}

TEST_F(ControlSourcePriorityTest, WifiOnly) {
  platform_.SetWifiCommand(RcCommand{0.4f, -0.2f});
  platform_.SetTimeMs(100);
  wifi_->Update(100, 2);

  float t = 0, s = 0;
  EXPECT_TRUE(SelectSource(t, s));
  EXPECT_FLOAT_EQ(t, 0.4f);
  EXPECT_FLOAT_EQ(s, -0.2f);
}

TEST_F(ControlSourcePriorityTest, RcOverridesWifi) {
  // Both sources active
  platform_.SetRcCommand(RcCommand{0.8f, 0.1f});
  platform_.SetWifiCommand(RcCommand{0.2f, -0.5f});
  platform_.SetTimeMs(20);
  rc_->Update(20, 20);
  wifi_->Update(20, 2);

  float t = 0, s = 0;
  EXPECT_TRUE(SelectSource(t, s));
  EXPECT_FLOAT_EQ(t, 0.8f) << "RC should have priority over WiFi";
  EXPECT_FLOAT_EQ(s, 0.1f);
}

TEST_F(ControlSourcePriorityTest, FallbackToWifi_WhenRcLost) {
  // RC present first
  platform_.SetRcCommand(RcCommand{0.8f, 0.1f});
  platform_.SetWifiCommand(RcCommand{0.2f, -0.5f});
  platform_.SetTimeMs(20);
  rc_->Update(20, 20);
  wifi_->Update(20, 2);

  // RC lost
  platform_.ClearRcCommand();
  platform_.SetTimeMs(40);
  rc_->Update(40, 20);

  float t = 0, s = 0;
  EXPECT_TRUE(SelectSource(t, s));
  EXPECT_FLOAT_EQ(t, 0.2f) << "Should fall back to WiFi when RC lost";
  EXPECT_FLOAT_EQ(s, -0.5f);
}
