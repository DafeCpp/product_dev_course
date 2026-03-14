#include <gtest/gtest.h>

#include <cJSON.h>

#include "control_components.hpp"
#include "mock_platform.hpp"

using namespace rc_vehicle;
using namespace rc_vehicle::testing;

// ══════════════════════════════════════════════════════════════════════════════
// TelemetryHandler
// ══════════════════════════════════════════════════════════════════════════════

class TelemetryHandlerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    platform_.SetWebSocketClientCount(1);
    handler_ = std::make_unique<TelemetryHandler>(platform_, 50);  // 50 ms
  }

  TelemetrySnapshot MakeSnap() {
    TelemetrySnapshot snap{};
    snap.rc_ok = true;
    snap.wifi_ok = false;
    snap.throttle = 0.5f;
    snap.steering = -0.3f;
    return snap;
  }

  FakePlatform platform_;
  std::unique_ptr<TelemetryHandler> handler_;
};

TEST_F(TelemetryHandlerTest, DoesNotSend_BeforeInterval) {
  handler_->Update(10, MakeSnap());
  EXPECT_EQ(platform_.GetTelemSendCount(), 0);
}

TEST_F(TelemetryHandlerTest, Sends_AtInterval) {
  handler_->Update(50, MakeSnap());
  EXPECT_EQ(platform_.GetTelemSendCount(), 1);
}

TEST_F(TelemetryHandlerTest, Sends_AtExactMultiples) {
  handler_->Update(50, MakeSnap());
  EXPECT_EQ(platform_.GetTelemSendCount(), 1);

  // Too early
  handler_->Update(80, MakeSnap());
  EXPECT_EQ(platform_.GetTelemSendCount(), 1);

  // At next interval
  handler_->Update(100, MakeSnap());
  EXPECT_EQ(platform_.GetTelemSendCount(), 2);
}

TEST_F(TelemetryHandlerTest, DoesNotSend_WhenNoClients) {
  platform_.SetWebSocketClientCount(0);
  handler_->Update(50, MakeSnap());
  EXPECT_EQ(platform_.GetTelemSendCount(), 0);
}

TEST_F(TelemetryHandlerTest, Sends_WhenClientsReappear) {
  platform_.SetWebSocketClientCount(0);
  handler_->Update(50, MakeSnap());
  EXPECT_EQ(platform_.GetTelemSendCount(), 0);

  platform_.SetWebSocketClientCount(2);
  handler_->Update(100, MakeSnap());
  EXPECT_EQ(platform_.GetTelemSendCount(), 1);
}

TEST_F(TelemetryHandlerTest, JsonContainsType) {
  handler_->Update(50, MakeSnap());
  const auto& json_str = platform_.GetLastTelem();
  ASSERT_FALSE(json_str.empty());

  cJSON* root = cJSON_Parse(json_str.c_str());
  ASSERT_NE(root, nullptr);

  cJSON* type = cJSON_GetObjectItem(root, "type");
  ASSERT_NE(type, nullptr);
  EXPECT_STREQ(type->valuestring, "telem");

  cJSON_Delete(root);
}

TEST_F(TelemetryHandlerTest, JsonContainsLinkStatus) {
  auto snap = MakeSnap();
  snap.rc_ok = true;
  snap.wifi_ok = false;

  handler_->Update(50, snap);
  cJSON* root = cJSON_Parse(platform_.GetLastTelem().c_str());
  ASSERT_NE(root, nullptr);

  cJSON* link = cJSON_GetObjectItem(root, "link");
  ASSERT_NE(link, nullptr);
  EXPECT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(link, "rc_ok")));
  EXPECT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(link, "wifi_ok")));

  cJSON_Delete(root);
}

TEST_F(TelemetryHandlerTest, JsonContainsImu_WhenEnabled) {
  auto snap = MakeSnap();
  snap.imu_enabled = true;
  snap.imu_data.ax = 0.01f;
  snap.imu_data.ay = 0.02f;
  snap.imu_data.az = 9.81f;
  snap.pitch_deg = 5.0f;
  snap.roll_deg = -2.0f;

  handler_->Update(50, snap);
  cJSON* root = cJSON_Parse(platform_.GetLastTelem().c_str());
  ASSERT_NE(root, nullptr);

  cJSON* imu = cJSON_GetObjectItem(root, "imu");
  ASSERT_NE(imu, nullptr);
  EXPECT_NEAR(cJSON_GetObjectItem(imu, "az")->valuedouble, 9.81, 0.01);

  cJSON* orientation = cJSON_GetObjectItem(imu, "orientation");
  ASSERT_NE(orientation, nullptr);
  EXPECT_NEAR(cJSON_GetObjectItem(orientation, "pitch")->valuedouble, 5.0,
              0.01);

  cJSON_Delete(root);
}

TEST_F(TelemetryHandlerTest, JsonNoImu_WhenDisabled) {
  auto snap = MakeSnap();
  snap.imu_enabled = false;

  handler_->Update(50, snap);
  cJSON* root = cJSON_Parse(platform_.GetLastTelem().c_str());
  ASSERT_NE(root, nullptr);

  EXPECT_EQ(cJSON_GetObjectItem(root, "imu"), nullptr);

  cJSON_Delete(root);
}

TEST_F(TelemetryHandlerTest, JsonContainsEkf_WhenAvailable) {
  auto snap = MakeSnap();
  snap.imu_enabled = true;
  snap.ekf_available = true;
  snap.ekf_vx = 1.5f;
  snap.ekf_vy = 0.1f;
  snap.ekf_speed_ms = 1.503f;

  handler_->Update(50, snap);
  cJSON* root = cJSON_Parse(platform_.GetLastTelem().c_str());
  ASSERT_NE(root, nullptr);

  cJSON* ekf = cJSON_GetObjectItem(root, "ekf");
  ASSERT_NE(ekf, nullptr);
  EXPECT_NEAR(cJSON_GetObjectItem(ekf, "vx")->valuedouble, 1.5, 0.01);
  EXPECT_NEAR(cJSON_GetObjectItem(ekf, "speed_ms")->valuedouble, 1.503, 0.01);

  cJSON_Delete(root);
}

TEST_F(TelemetryHandlerTest, JsonContainsKidsMode) {
  auto snap = MakeSnap();
  snap.kids_mode_active = true;
  snap.kids_throttle_limit = 0.15f;

  handler_->Update(50, snap);
  cJSON* root = cJSON_Parse(platform_.GetLastTelem().c_str());
  ASSERT_NE(root, nullptr);

  cJSON* kids = cJSON_GetObjectItem(root, "kids_mode");
  ASSERT_NE(kids, nullptr);
  EXPECT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(kids, "active")));
  EXPECT_NEAR(cJSON_GetObjectItem(kids, "throttle_limit")->valuedouble, 0.15,
              0.01);

  cJSON_Delete(root);
}
