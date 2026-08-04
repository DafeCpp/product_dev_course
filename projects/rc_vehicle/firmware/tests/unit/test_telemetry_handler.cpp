#include <cJSON.h>
#include <gtest/gtest.h>

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
  handler_->SendTelemetry(10, MakeSnap());
  EXPECT_EQ(platform_.GetTelemSendCount(), 0);
}

TEST_F(TelemetryHandlerTest, Sends_AtInterval) {
  handler_->SendTelemetry(50, MakeSnap());
  EXPECT_EQ(platform_.GetTelemSendCount(), 1);
}

TEST_F(TelemetryHandlerTest, Sends_AtExactMultiples) {
  handler_->SendTelemetry(50, MakeSnap());
  EXPECT_EQ(platform_.GetTelemSendCount(), 1);

  // Too early
  handler_->SendTelemetry(80, MakeSnap());
  EXPECT_EQ(platform_.GetTelemSendCount(), 1);

  // At next interval
  handler_->SendTelemetry(100, MakeSnap());
  EXPECT_EQ(platform_.GetTelemSendCount(), 2);
}

// Debug-режим 100 Гц: интервал отправки конфигурируем
// (RC_TELEM_SEND_INTERVAL_MS), а не захардкожен. Проверяем, что
// TelemetryHandler с интервалом 10 мс шлёт на каждом 10-мс тике (100 Гц) и не
// чаще.
TEST_F(TelemetryHandlerTest, Sends_At100Hz_WhenInterval10ms) {
  TelemetryHandler fast_handler(platform_, 10);  // 10 ms = 100 Hz

  fast_handler.SendTelemetry(10, MakeSnap());
  EXPECT_EQ(platform_.GetTelemSendCount(), 1);

  // 5 мс позже — ещё рано для следующего кадра
  fast_handler.SendTelemetry(15, MakeSnap());
  EXPECT_EQ(platform_.GetTelemSendCount(), 1);

  // На следующем 10-мс интервале
  fast_handler.SendTelemetry(20, MakeSnap());
  EXPECT_EQ(platform_.GetTelemSendCount(), 2);

  fast_handler.SendTelemetry(30, MakeSnap());
  EXPECT_EQ(platform_.GetTelemSendCount(), 3);
}

// FW-R13: control loop больше НЕ гейтит телеметрию по числу клиентов —
// решение о доставке принимает транспорт (WebSocketSendTelem шлёт только
// реальным WS-fd). Поэтому SendTelemetry ставит кадр в очередь независимо
// от GetWebSocketClientCount() (раньше при count==0 был ранний выход, что
// из-за ненадёжного bootstrap счётчика приводило к пустому Web UI).
TEST_F(TelemetryHandlerTest, Sends_EvenWhenClientCountZero) {
  platform_.SetWebSocketClientCount(0);
  handler_->SendTelemetry(50, MakeSnap());
  EXPECT_EQ(platform_.GetTelemSendCount(), 1);
}

TEST_F(TelemetryHandlerTest, Sends_RegardlessOfClientCountChanges) {
  platform_.SetWebSocketClientCount(0);
  handler_->SendTelemetry(50, MakeSnap());
  EXPECT_EQ(platform_.GetTelemSendCount(), 1);

  platform_.SetWebSocketClientCount(2);
  handler_->SendTelemetry(100, MakeSnap());
  EXPECT_EQ(platform_.GetTelemSendCount(), 2);
}

TEST_F(TelemetryHandlerTest, JsonContainsType) {
  handler_->SendTelemetry(50, MakeSnap());
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

  handler_->SendTelemetry(50, snap);
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

  handler_->SendTelemetry(50, snap);
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

  handler_->SendTelemetry(50, snap);
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

  handler_->SendTelemetry(50, snap);
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
  snap.kids_accel_limit_active = true;
  snap.kids_limiters_enabled = true;
  snap.kids_throttle_limit = 0.15f;

  handler_->SendTelemetry(50, snap);
  cJSON* root = cJSON_Parse(platform_.GetLastTelem().c_str());
  ASSERT_NE(root, nullptr);

  cJSON* kids = cJSON_GetObjectItem(root, "kids_mode");
  ASSERT_NE(kids, nullptr);
  EXPECT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(kids, "active")));
  // Каждый лимитер отдаётся отдельным ключом: по одному active нельзя понять,
  // какой именно ограничитель режет газ (LOS-13).
  EXPECT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(kids, "accel_limit_active")));
  EXPECT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(kids, "limiters_enabled")));
  EXPECT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(kids, "anti_spin_active")));
  EXPECT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(kids, "speed_limit_active")));
  EXPECT_NEAR(cJSON_GetObjectItem(kids, "throttle_limit")->valuedouble, 0.15,
              0.01);

  cJSON_Delete(root);
}

// ─── FW-RF8: телеметрия развязана с control loop ────────────────────────────

// SendTelemetry публикует POD-снимок в платформу (PublishTelem), а не строит
// JSON. Снимок доходит до платформы как есть.
TEST_F(TelemetryHandlerTest, PublishesSnapshotToPlatform) {
  auto snap = MakeSnap();
  snap.throttle = 0.42f;

  handler_->SendTelemetry(50, snap);

  EXPECT_EQ(platform_.GetTelemSendCount(), 1);
  EXPECT_FLOAT_EQ(platform_.GetLastSnap().throttle, 0.42f);
}

// failsafe берётся из снимка (а не из платформы во время постройки JSON) —
// это и позволяет строить JSON вне control loop.
TEST_F(TelemetryHandlerTest, FailsafeComesFromSnapshotNotPlatform) {
  // Платформа НЕ в failsafe, но снимок говорит обратное → в JSON true.
  platform_.SetFailsafeActive(false);
  auto snap = MakeSnap();
  snap.failsafe = true;

  handler_->SendTelemetry(50, snap);
  cJSON* root = cJSON_Parse(platform_.GetLastTelem().c_str());
  ASSERT_NE(root, nullptr);

  cJSON* link = cJSON_GetObjectItem(root, "link");
  ASSERT_NE(link, nullptr);
  EXPECT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(link, "failsafe")));

  cJSON_Delete(root);
}

// BuildTelemJson — чистая функция от снимка (вызывается в задаче телеметрии).
TEST(BuildTelemJsonTest, PureFunctionReflectsSnapshot) {
  TelemetrySnapshot snap{};
  snap.rc_ok = true;
  snap.failsafe = false;
  snap.uptime_ms = 12345;

  std::string json = BuildTelemJson(snap);
  cJSON* root = cJSON_Parse(json.c_str());
  ASSERT_NE(root, nullptr);

  EXPECT_STREQ(cJSON_GetObjectItem(root, "type")->valuestring, "telem");
  EXPECT_NEAR(cJSON_GetObjectItem(root, "uptime_ms")->valuedouble, 12345, 0.5);
  cJSON* link = cJSON_GetObjectItem(root, "link");
  ASSERT_NE(link, nullptr);
  EXPECT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(link, "failsafe")));

  cJSON_Delete(root);
}

TEST(BuildTelemJsonTest, IncludesZuptStatus) {
  TelemetrySnapshot snap{};
  snap.imu_enabled = true;
  snap.ekf_available = true;
  snap.ekf_zupt_status = ZuptStatus::ThrottleRejected;

  std::string json = BuildTelemJson(snap);
  cJSON* root = cJSON_Parse(json.c_str());
  ASSERT_NE(root, nullptr);

  cJSON* ekf = cJSON_GetObjectItem(root, "ekf");
  ASSERT_NE(ekf, nullptr);
  cJSON* status = cJSON_GetObjectItem(ekf, "zupt_status");
  ASSERT_NE(status, nullptr);
  EXPECT_STREQ(status->valuestring, "throttle_rejected");

  cJSON_Delete(root);
}

// LOS-252: BuildTelemJson больше не использует cJSON (см. JsonWriter в
// firmware_common). Кадр на полностью заполненном снимке должен остаться
// валидным JSON с теми же ключами и значениями (в пределах точности
// JsonWriter::Fixed) и заметно короче старого 17-значного вывода cJSON.
TEST(BuildTelemJsonTest, FullSnapshotProducesValidJsonWithAllKeys) {
  TelemetrySnapshot snap{};
  snap.rc_ok = true;
  snap.wifi_ok = true;
  snap.failsafe = false;
  snap.uptime_ms = 987654;

  snap.imu_enabled = true;
  snap.imu_data = {.ax = 0.011f,
                   .ay = -0.022f,
                   .az = 9.803f,
                   .gx = 1.25f,
                   .gy = -3.5f,
                   .gz = 0.0f};
  snap.filtered_gz = 0.12f;
  snap.forward_accel = -0.5f;
  snap.pitch_deg = 5.5f;
  snap.roll_deg = -2.25f;
  snap.yaw_deg = 179.99f;

  snap.mag_enabled = true;
  snap.mag_data = {.mx = 123.4f, .my = -56.7f, .mz = 8.9f};
  snap.heading_deg = 45.5f;
  snap.heading_rel_deg = -10.25f;

  snap.calib_status = CalibStatus::Done;
  snap.calib_stage = 3;
  snap.calib_valid = true;
  snap.calib_data.gyro_bias[0] = 0.0011f;
  snap.calib_data.gyro_bias[1] = -0.0022f;
  snap.calib_data.gyro_bias[2] = 0.0033f;
  snap.calib_data.accel_bias[0] = 0.001f;
  snap.calib_data.accel_bias[1] = -0.002f;
  snap.calib_data.accel_bias[2] = 0.003f;
  snap.calib_data.gravity_vec[0] = 0.01f;
  snap.calib_data.gravity_vec[1] = 0.02f;
  snap.calib_data.gravity_vec[2] = 0.999f;
  snap.calib_data.accel_forward_vec[0] = 0.998f;
  snap.calib_data.accel_forward_vec[1] = 0.03f;
  snap.calib_data.accel_forward_vec[2] = -0.04f;

  snap.ekf_available = true;
  snap.ekf_vx = 1.234f;
  snap.ekf_vy = -0.056f;
  snap.ekf_yaw_rate = 0.789f;
  snap.ekf_slip_deg = 3.14f;
  snap.ekf_speed_ms = 1.5f;
  snap.ekf_vx_var = 1.23e-7f;
  snap.ekf_vy_var = 0.0f;
  snap.ekf_r_var = 0.002f;
  snap.ekf_zupt_status = ZuptStatus::Applied;
  snap.ekf_speed_meas = 1.6f;
  snap.ekf_diverged = false;

  snap.oversteer_available = true;
  snap.oversteer_active = true;

  snap.kids_mode_active = true;
  snap.kids_anti_spin_active = true;
  snap.kids_accel_limit_active = true;
  snap.kids_speed_limit_active = true;
  snap.kids_limiters_enabled = true;
  snap.kids_throttle_limit = 0.3f;

  snap.rc_throttle = 0.7f;
  snap.rc_steering = -0.4f;
  snap.cmd_throttle = 0.65f;
  snap.cmd_steering = -0.35f;
  snap.throttle = 0.6f;
  snap.steering = -0.3f;

  const std::string json = BuildTelemJson(snap);

  cJSON* root = cJSON_Parse(json.c_str());
  ASSERT_NE(root, nullptr) << "not valid JSON: " << json;

  EXPECT_STREQ(cJSON_GetObjectItem(root, "type")->valuestring, "telem");
  EXPECT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(root, "mcu_pong_ok")));
  EXPECT_NEAR(cJSON_GetObjectItem(root, "uptime_ms")->valuedouble, 987654, 0.5);

  cJSON* link = cJSON_GetObjectItem(root, "link");
  ASSERT_NE(link, nullptr);
  EXPECT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(link, "rc_ok")));
  EXPECT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(link, "wifi_ok")));
  EXPECT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(link, "failsafe")));

  cJSON* imu = cJSON_GetObjectItem(root, "imu");
  ASSERT_NE(imu, nullptr);
  EXPECT_NEAR(cJSON_GetObjectItem(imu, "ax")->valuedouble, 0.011, 0.001);
  EXPECT_NEAR(cJSON_GetObjectItem(imu, "gy")->valuedouble, -3.5, 0.01);
  EXPECT_NEAR(cJSON_GetObjectItem(imu, "gyro_z_filtered")->valuedouble, 0.12,
              0.01);
  EXPECT_NEAR(cJSON_GetObjectItem(imu, "forward_accel")->valuedouble, -0.5,
              0.001);
  cJSON* orientation = cJSON_GetObjectItem(imu, "orientation");
  ASSERT_NE(orientation, nullptr);
  EXPECT_NEAR(cJSON_GetObjectItem(orientation, "yaw")->valuedouble, 179.99,
              0.01);

  cJSON* calib = cJSON_GetObjectItem(root, "calib");
  ASSERT_NE(calib, nullptr);
  EXPECT_STREQ(cJSON_GetObjectItem(calib, "status")->valuestring, "done");
  EXPECT_NEAR(cJSON_GetObjectItem(calib, "stage")->valuedouble, 3, 0.5);
  EXPECT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(calib, "valid")));
  cJSON* bias = cJSON_GetObjectItem(calib, "bias");
  ASSERT_NE(bias, nullptr);
  EXPECT_NEAR(cJSON_GetObjectItem(bias, "gz")->valuedouble, 0.0033, 0.0001);
  cJSON* gravity = cJSON_GetObjectItem(calib, "gravity_vec");
  ASSERT_NE(gravity, nullptr);
  ASSERT_EQ(cJSON_GetArraySize(gravity), 3);
  EXPECT_NEAR(cJSON_GetArrayItem(gravity, 2)->valuedouble, 0.999, 0.0001);
  cJSON* forward = cJSON_GetObjectItem(calib, "forward_vec");
  ASSERT_NE(forward, nullptr);
  ASSERT_EQ(cJSON_GetArraySize(forward), 3);
  EXPECT_NEAR(cJSON_GetArrayItem(forward, 0)->valuedouble, 0.998, 0.0001);

  cJSON* mag = cJSON_GetObjectItem(root, "mag");
  ASSERT_NE(mag, nullptr);
  EXPECT_NEAR(cJSON_GetObjectItem(mag, "mx")->valuedouble, 123.4, 0.1);
  EXPECT_NEAR(cJSON_GetObjectItem(mag, "heading_deg")->valuedouble, 45.5, 0.01);

  cJSON* ekf = cJSON_GetObjectItem(root, "ekf");
  ASSERT_NE(ekf, nullptr);
  EXPECT_NEAR(cJSON_GetObjectItem(ekf, "vx")->valuedouble, 1.234, 0.001);
  EXPECT_NEAR(cJSON_GetObjectItem(ekf, "vy_var")->valuedouble, 0.0, 1e-9);
  EXPECT_NEAR(cJSON_GetObjectItem(ekf, "vx_var")->valuedouble, 1.23e-7, 1e-8);
  EXPECT_STREQ(cJSON_GetObjectItem(ekf, "zupt_status")->valuestring, "applied");
  EXPECT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(ekf, "diverged")));

  cJSON* warn = cJSON_GetObjectItem(root, "warn");
  ASSERT_NE(warn, nullptr);
  EXPECT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(warn, "oversteer")));

  cJSON* kids = cJSON_GetObjectItem(root, "kids_mode");
  ASSERT_NE(kids, nullptr);
  EXPECT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(kids, "anti_spin_active")));
  EXPECT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(kids, "accel_limit_active")));
  EXPECT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(kids, "speed_limit_active")));
  EXPECT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(kids, "limiters_enabled")));
  EXPECT_NEAR(cJSON_GetObjectItem(kids, "throttle_limit")->valuedouble, 0.3,
              0.001);

  cJSON* rc = cJSON_GetObjectItem(root, "rc");
  ASSERT_NE(rc, nullptr);
  EXPECT_NEAR(cJSON_GetObjectItem(rc, "throttle")->valuedouble, 0.7, 0.001);

  cJSON* cmd = cJSON_GetObjectItem(root, "cmd");
  ASSERT_NE(cmd, nullptr);
  EXPECT_NEAR(cJSON_GetObjectItem(cmd, "steering")->valuedouble, -0.35, 0.001);

  cJSON* act = cJSON_GetObjectItem(root, "act");
  ASSERT_NE(act, nullptr);
  EXPECT_NEAR(cJSON_GetObjectItem(act, "throttle")->valuedouble, 0.6, 0.001);

  cJSON_Delete(root);

  // Регресс-гейт: полный кадр должен оставаться заметно короче старого
  // cJSON-вывода (тот легко уходил за 2 КБ из-за 17-значных double).
  EXPECT_LT(json.size(), 1152u) << "frame: " << json;
}

// Все блоки imu/calib/mag/ekf/warn сгруппированы под imu_enabled — при
// выключенном IMU их не должно быть вовсе (в т.ч. kids_mode/rc, которые под
// своими собственными условиями).
TEST(BuildTelemJsonTest, EmptySnapshotOmitsOptionalBlocks) {
  TelemetrySnapshot
      snap{};  // всё по умолчанию: imu_enabled=false, rc_ok=false, ...

  const std::string json = BuildTelemJson(snap);
  cJSON* root = cJSON_Parse(json.c_str());
  ASSERT_NE(root, nullptr) << "not valid JSON: " << json;

  EXPECT_EQ(cJSON_GetObjectItem(root, "imu"), nullptr);
  EXPECT_EQ(cJSON_GetObjectItem(root, "calib"), nullptr);
  EXPECT_EQ(cJSON_GetObjectItem(root, "mag"), nullptr);
  EXPECT_EQ(cJSON_GetObjectItem(root, "ekf"), nullptr);
  EXPECT_EQ(cJSON_GetObjectItem(root, "warn"), nullptr);
  EXPECT_EQ(cJSON_GetObjectItem(root, "kids_mode"), nullptr);
  EXPECT_EQ(cJSON_GetObjectItem(root, "rc"), nullptr);

  // cmd/act публикуются безусловно.
  EXPECT_NE(cJSON_GetObjectItem(root, "cmd"), nullptr);
  EXPECT_NE(cJSON_GetObjectItem(root, "act"), nullptr);

  cJSON_Delete(root);
}
