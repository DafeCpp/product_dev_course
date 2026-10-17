#include <cJSON.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "../../esp32_s3/main/ws_command_registry.hpp"
#include "esp_http_server.h"
#include "i_vehicle_control.hpp"

// LOS-37: ws_command_registry.cpp без железа. esp_http_server.h подменён моком
// (tests/mocks/esp_http_server.h), который записывает отправленные кадры.

using namespace rc_vehicle;

namespace {

/**
 * Заглушка IVehicleControl: реестру нужен только IsReady(), остальные методы
 * возвращают значения по умолчанию. Контракт реестра проверяется через
 * хендлеры и отправленные кадры, а не через вызовы заглушки.
 */
class StubVehicleControl : public IVehicleControl {
 public:
  bool ready{true};

  [[nodiscard]] bool IsReady() const noexcept override { return ready; }

  void OnWifiCommand(float, float) override {}
  void StartCalibration(bool) override {}
  bool StartForwardCalibration() override { return false; }
  bool StartAutoForwardCalibration(float) override { return false; }
  [[nodiscard]] const char* GetCalibStatus() const override { return "idle"; }
  [[nodiscard]] int GetCalibStage() const override { return 0; }
  void SetForwardDirection(float, float, float) override {}
  [[nodiscard]] StabilizationConfig GetStabilizationConfig() const override {
    return {};
  }
  bool SetStabilizationConfig(const StabilizationConfig&, bool) override {
    return false;
  }
  void SetKidsModeActive(bool) override {}
  [[nodiscard]] bool IsKidsModeActive() const override { return false; }
  bool StartSteeringTrimCalibration(float) override { return false; }
  void StopSteeringTrimCalibration() override {}
  [[nodiscard]] bool IsSteeringTrimCalibActive() const override {
    return false;
  }
  [[nodiscard]] SteeringTrimCalibration::Result GetSteeringTrimCalibResult()
      const override {
    return {};
  }
  bool StartComOffsetCalibration(float, float, float) override { return false; }
  void StopComOffsetCalibration() override {}
  [[nodiscard]] bool IsComOffsetCalibActive() const override { return false; }
  [[nodiscard]] ComOffsetCalibration::Result GetComOffsetCalibResult()
      const override {
    return {};
  }
  bool StartTest(const TestParams&) override { return false; }
  void StopTest() override {}
  [[nodiscard]] bool IsTestActive() const override { return false; }
  [[nodiscard]] TestRunner::Status GetTestStatus() const override { return {}; }
  bool StartSpeedCalibration(float, float) override { return false; }
  void StopSpeedCalibration() override {}
  [[nodiscard]] bool IsSpeedCalibActive() const override { return false; }
  [[nodiscard]] SpeedCalibration::Result GetSpeedCalibResult() const override {
    return {};
  }
  void ResetHeadingRef() override {}
  bool StartMagCalibration() override { return false; }
  bool FinishMagCalibration() override { return false; }
  bool CancelMagCalibration() override { return false; }
  MagCalibEraseAck EraseMagCalibration() override { return {}; }
  [[nodiscard]] MagCalibStateView GetMagCalibState() const override {
    return {};
  }
  void GetLogInfo(size_t& count_out, size_t& cap_out) const override {
    count_out = 0;
    cap_out = 0;
  }
  [[nodiscard]] bool GetLogFrame(size_t, TelemetryLogFrame&) const override {
    return false;
  }
  [[nodiscard]] bool BeginLogExport(size_t& count_out) override {
    count_out = 0;
    return false;
  }
  [[nodiscard]] bool BeginLogAndConfigExport(size_t&, TelemetryLogFrame&,
                                             size_t&) override {
    return false;
  }
  [[nodiscard]] bool FinalizeConfigSnapshotExport(size_t&) override {
    return false;
  }
  [[nodiscard]] size_t CopyLogExportFrames(size_t, TelemetryLogFrame*,
                                           size_t) const override {
    return 0;
  }
  void EndLogExport() override {}
  void ClearLog() override {}
  [[nodiscard]] size_t GetEventCount() const override { return 0; }
  [[nodiscard]] bool GetEvent(size_t, TelemetryEvent&) const override {
    return false;
  }
  void ClearEventLog() override {}
  [[nodiscard]] size_t GetConfigSnapshotCount() const override { return 0; }
  [[nodiscard]] bool GetConfigSnapshot(
      size_t, TelemetryConfigSnapshot&) const override {
    return false;
  }
  [[nodiscard]] size_t CopyConfigSnapshots(TelemetryConfigSnapshot*,
                                           size_t) const override {
    return 0;
  }
  [[nodiscard]] bool BeginConfigSnapshotExport(uint32_t, size_t&) override {
    return false;
  }
  [[nodiscard]] bool GetNextConfigSnapshotExport(
      TelemetryConfigSnapshot&) override {
    return false;
  }
  void EndConfigSnapshotExport() override {}
  [[nodiscard]] std::vector<SelfTestItem> RunSelfTest() const override {
    return {};
  }
};

/** Что видел хендлер при вызове. */
struct HandlerCall {
  int count{0};
  IVehicleControl* vc{nullptr};
  cJSON* json{nullptr};
  httpd_req_t* req{nullptr};
};

/** Разобрать payload отправленного кадра как JSON (nullptr, если не JSON). */
class ParsedJson {
 public:
  explicit ParsedJson(const std::string& text)
      : root_(cJSON_Parse(text.c_str())) {}
  ~ParsedJson() { cJSON_Delete(root_); }
  ParsedJson(const ParsedJson&) = delete;
  ParsedJson& operator=(const ParsedJson&) = delete;

  [[nodiscard]] cJSON* get() const { return root_; }
  [[nodiscard]] std::string Str(const char* key) const {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root_, key);
    return cJSON_IsString(item) ? item->valuestring : std::string{};
  }

 private:
  cJSON* root_;
};

}  // namespace

class WsCommandRegistryTest : public ::testing::Test {
 protected:
  void SetUp() override { http_server_mock::Reset(); }

  WsJsonHandler Recorder(HandlerCall& call) {
    return [&call](IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
      ++call.count;
      call.vc = &vc;
      call.json = json;
      call.req = req;
    };
  }

  WsCommandRegistry registry_;
  StubVehicleControl vc_;
  httpd_req_t req_;
};

// ─── Register / HasHandler / GetHandlerCount ────────────────────────────────

TEST_F(WsCommandRegistryTest, EmptyRegistry_HasNoHandlers) {
  EXPECT_EQ(registry_.GetHandlerCount(), 0u);
  EXPECT_FALSE(registry_.HasHandler("calibrate_imu"));
}

TEST_F(WsCommandRegistryTest, Register_AddsHandler) {
  HandlerCall call;
  registry_.Register("calibrate_imu", Recorder(call));

  EXPECT_EQ(registry_.GetHandlerCount(), 1u);
  EXPECT_TRUE(registry_.HasHandler("calibrate_imu"));
  EXPECT_FALSE(registry_.HasHandler("other"));
}

TEST_F(WsCommandRegistryTest, Register_NullHandler_IsIgnored) {
  registry_.Register("calibrate_imu", WsJsonHandler{});

  EXPECT_EQ(registry_.GetHandlerCount(), 0u);
  EXPECT_FALSE(registry_.HasHandler("calibrate_imu"));
}

TEST_F(WsCommandRegistryTest, Register_SameTypeTwice_ReplacesHandler) {
  HandlerCall first;
  HandlerCall second;
  registry_.Register("cmd", Recorder(first));
  registry_.Register("cmd", Recorder(second));

  EXPECT_EQ(registry_.GetHandlerCount(), 1u);
  cJSON* json = cJSON_CreateObject();
  EXPECT_TRUE(registry_.Handle(vc_, "cmd", json, &req_));
  cJSON_Delete(json);

  EXPECT_EQ(first.count, 0);
  EXPECT_EQ(second.count, 1);
}

TEST_F(WsCommandRegistryTest, Register_NullHandlerDoesNotRemoveExisting) {
  HandlerCall call;
  registry_.Register("cmd", Recorder(call));
  registry_.Register("cmd", WsJsonHandler{});

  EXPECT_TRUE(registry_.HasHandler("cmd"));
  EXPECT_EQ(registry_.GetHandlerCount(), 1u);
}

TEST_F(WsCommandRegistryTest, HasHandler_NullType_ReturnsFalse) {
  EXPECT_FALSE(registry_.HasHandler(nullptr));
}

// ─── Handle ─────────────────────────────────────────────────────────────────

TEST_F(WsCommandRegistryTest, Handle_CallsRegisteredHandlerWithArguments) {
  HandlerCall call;
  registry_.Register("calibrate_imu", Recorder(call));
  cJSON* json = cJSON_CreateObject();

  EXPECT_TRUE(registry_.Handle(vc_, "calibrate_imu", json, &req_));

  EXPECT_EQ(call.count, 1);
  EXPECT_EQ(call.vc, &vc_);
  EXPECT_EQ(call.json, json);
  EXPECT_EQ(call.req, &req_);
  EXPECT_EQ(http_server_mock::GetState().send_calls, 0);
  cJSON_Delete(json);
}

TEST_F(WsCommandRegistryTest, Handle_DispatchesByType) {
  HandlerCall a;
  HandlerCall b;
  registry_.Register("a", Recorder(a));
  registry_.Register("b", Recorder(b));

  EXPECT_TRUE(registry_.Handle(vc_, "b", nullptr, &req_));
  EXPECT_TRUE(registry_.Handle(vc_, "b", nullptr, &req_));
  EXPECT_TRUE(registry_.Handle(vc_, "a", nullptr, &req_));

  EXPECT_EQ(a.count, 1);
  EXPECT_EQ(b.count, 2);
}

TEST_F(WsCommandRegistryTest, Handle_UnknownType_ReturnsFalse) {
  HandlerCall call;
  registry_.Register("known", Recorder(call));

  EXPECT_FALSE(registry_.Handle(vc_, "unknown", nullptr, &req_));
  EXPECT_EQ(call.count, 0);
  EXPECT_EQ(http_server_mock::GetState().send_calls, 0);
}

TEST_F(WsCommandRegistryTest, Handle_NullType_ReturnsFalse) {
  HandlerCall call;
  registry_.Register("known", Recorder(call));

  EXPECT_FALSE(registry_.Handle(vc_, nullptr, nullptr, &req_));
  EXPECT_EQ(call.count, 0);
  EXPECT_EQ(http_server_mock::GetState().send_calls, 0);
}

TEST_F(WsCommandRegistryTest, Handle_NotReady_RejectsWithErrorReply) {
  HandlerCall call;
  registry_.Register("calibrate_imu", Recorder(call));
  vc_.ready = false;

  // Команда «обработана» (отклонена): true, хендлер не вызывается.
  EXPECT_TRUE(registry_.Handle(vc_, "calibrate_imu", nullptr, &req_));
  EXPECT_EQ(call.count, 0);

  const auto& state = http_server_mock::GetState();
  ASSERT_EQ(state.frames.size(), 1u);
  EXPECT_EQ(state.frames[0].req, &req_);
  EXPECT_EQ(state.frames[0].type, HTTPD_WS_TYPE_TEXT);
  ParsedJson reply(state.frames[0].payload);
  ASSERT_NE(reply.get(), nullptr);
  EXPECT_EQ(reply.Str("type"), "error");
  EXPECT_EQ(reply.Str("error"), "not_ready");
  EXPECT_EQ(reply.Str("message"), "Control task is still initializing");
}

TEST_F(WsCommandRegistryTest, Handle_NotReady_RejectsUnknownTypeToo) {
  vc_.ready = false;

  // Барьер срабатывает до поиска хендлера: клиент получает not_ready, а не
  // молчаливое false, даже если типа в реестре нет.
  EXPECT_TRUE(registry_.Handle(vc_, "no_such_command", nullptr, &req_));
  ASSERT_EQ(http_server_mock::GetState().frames.size(), 1u);
}

TEST_F(WsCommandRegistryTest,
       Handle_NotReady_NullType_ReturnsFalseWithoutReply) {
  vc_.ready = false;

  EXPECT_FALSE(registry_.Handle(vc_, nullptr, nullptr, &req_));
  EXPECT_EQ(http_server_mock::GetState().send_calls, 0);
}

TEST_F(WsCommandRegistryTest, Handle_BecomesReady_DispatchesAgain) {
  HandlerCall call;
  registry_.Register("cmd", Recorder(call));
  vc_.ready = false;
  EXPECT_TRUE(registry_.Handle(vc_, "cmd", nullptr, &req_));
  EXPECT_EQ(call.count, 0);

  vc_.ready = true;
  EXPECT_TRUE(registry_.Handle(vc_, "cmd", nullptr, &req_));
  EXPECT_EQ(call.count, 1);
}

// ─── WsSendJsonReply ────────────────────────────────────────────────────────

TEST_F(WsCommandRegistryTest, SendJsonReply_SendsSingleTextFrame) {
  cJSON* reply = cJSON_CreateObject();
  cJSON_AddStringToObject(reply, "type", "pong");
  cJSON_AddNumberToObject(reply, "seq", 7);

  WsSendJsonReply(&req_, reply);

  const auto& state = http_server_mock::GetState();
  ASSERT_EQ(state.frames.size(), 1u);
  const auto& frame = state.frames[0];
  EXPECT_EQ(frame.req, &req_);
  EXPECT_EQ(frame.type, HTTPD_WS_TYPE_TEXT);
  EXPECT_TRUE(frame.final);
  EXPECT_FALSE(frame.fragmented);
  EXPECT_EQ(frame.payload, R"({"type":"pong","seq":7})");

  // Вызывающий владеет reply: функция его не освобождает.
  EXPECT_STREQ(cJSON_GetObjectItemCaseSensitive(reply, "type")->valuestring,
               "pong");
  cJSON_Delete(reply);
}

TEST_F(WsCommandRegistryTest, SendJsonReply_NullRequest_SendsNothing) {
  cJSON* reply = cJSON_CreateObject();

  WsSendJsonReply(nullptr, reply);

  EXPECT_EQ(http_server_mock::GetState().send_calls, 0);
  cJSON_Delete(reply);
}

TEST_F(WsCommandRegistryTest, SendJsonReply_NullReply_SendsNothing) {
  WsSendJsonReply(&req_, nullptr);

  EXPECT_EQ(http_server_mock::GetState().send_calls, 0);
}

TEST_F(WsCommandRegistryTest, SendJsonReply_SendFailure_DoesNotThrowOrRetry) {
  http_server_mock::GetState().send_result = ESP_FAIL;
  cJSON* reply = cJSON_CreateObject();
  cJSON_AddStringToObject(reply, "type", "pong");

  EXPECT_NO_THROW(WsSendJsonReply(&req_, reply));

  EXPECT_EQ(http_server_mock::GetState().send_calls, 1);
  cJSON_Delete(reply);
}
