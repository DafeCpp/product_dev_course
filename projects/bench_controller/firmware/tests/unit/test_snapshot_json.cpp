// JSON-контракт Ф2: построитель снимка/ack и разбор команд.
// Схема: docs/experiment-control/bench-command-schema.md

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>

#include "snapshot_json.hpp"

namespace bench {
namespace {

std::string Snapshot(const BenchStateSnapshot& s) {
  char buf[kSnapshotJsonMaxSize];
  const size_t n = BuildSnapshotJson(s, buf, sizeof(buf));
  return std::string(buf, n);
}

bool Has(const std::string& hay, const char* needle) {
  return hay.find(needle) != std::string::npos;
}

bool Balanced(const std::string& s) {
  int braces = 0, brackets = 0;
  bool in_str = false;
  for (char c : s) {
    if (c == '"') in_str = !in_str;
    if (in_str) continue;
    braces += (c == '{') - (c == '}');
    brackets += (c == '[') - (c == ']');
    if (braces < 0 || brackets < 0) return false;
  }
  return braces == 0 && brackets == 0 && !in_str;
}

TEST(SnapshotJson, DefaultSnapshotHasAllSections) {
  const std::string j = Snapshot(BenchStateSnapshot{});
  EXPECT_TRUE(Balanced(j));
  for (const char* key :
       {"\"v\":1", "\"type\":\"snapshot\"", "\"mode\":\"displacement\"",
        "\"enabled\":true", "\"link_state\":\"running\"", "\"holding\":false",
        "\"stop_reason\":\"none\"", "\"estop_chain\":\"unknown\"",
        "\"setpoint\":{", "\"feedback\":{", "\"counters\":{", "\"acks\":[]",
        "\"tick_us\":{", "\"journal\":[]"}) {
    EXPECT_TRUE(Has(j, key)) << key << "\n" << j;
  }
}

TEST(SnapshotJson, CarriesValuesAcksAndJournal) {
  BenchStateSnapshot s{};
  s.seq = 7;
  s.mode = ControlMode::kForce;
  s.link_state = LinkState::kRampDown;
  s.holding = true;
  s.last_stop_reason = StopReason::kSpecimenFailure;
  s.estop_chain = EstopChainState::kOpen;
  s.force_n = 1234.5f;
  s.cmd_overwritten = 2;
  s.acks[0] = {.cmd_id = 9,
               .type = CommandType::kSetSetpoint,
               .result = CommandResult::kRejected,
               .reason = RejectReason::kProgramRunning,
               .duplicate = true};
  s.ack_count = 1;
  s.journal[0] = {.seq = 4,
                  .t_ms = 120,
                  .cmd_id = 9,
                  .kind = JournalKind::kCommand,
                  .arg0 = static_cast<uint8_t>(CommandType::kEStopSoft),
                  .arg1 = 0,
                  .arg2 = 0,
                  .flags = 1};
  s.journal[1] = {.seq = 5,
                  .t_ms = 121,
                  .kind = JournalKind::kLinkState,
                  .arg0 = 0,
                  .arg1 = 1};
  s.journal[2] = {.seq = 6,
                  .t_ms = 122,
                  .kind = JournalKind::kHold,
                  .arg0 = static_cast<uint8_t>(StopReason::kLinkSafeHold)};
  s.journal[3] = {.seq = 7, .t_ms = 123, .kind = JournalKind::kFailureLatch};
  s.journal_count = 4;
  s.journal_total = 8;

  const std::string j = Snapshot(s);
  EXPECT_TRUE(Balanced(j)) << j;
  EXPECT_TRUE(Has(j, "\"seq\":7"));
  EXPECT_TRUE(Has(j, "\"mode\":\"force\""));
  EXPECT_TRUE(Has(j, "\"link_state\":\"ramp_down\""));
  EXPECT_TRUE(Has(j, "\"stop_reason\":\"specimen_failure\""));
  EXPECT_TRUE(Has(j, "\"estop_chain\":\"open\""));
  EXPECT_TRUE(Has(j, "\"force_n\":1234.500"));
  EXPECT_TRUE(Has(j, "\"cmd_overwritten\":2"));
  EXPECT_TRUE(
      Has(j,
          "{\"cmd_id\":9,\"cmd\":\"set_setpoint\",\"result\":\"rejected\","
          "\"reason\":\"program_running\",\"duplicate\":true}"));
  EXPECT_TRUE(Has(j, "\"cmd\":\"e_stop_soft\""));
  EXPECT_TRUE(Has(j, "\"from\":\"running\",\"to\":\"grace\""));
  EXPECT_TRUE(Has(j, "\"kind\":\"hold\",\"reason\":\"link_safe_hold\""));
  EXPECT_TRUE(Has(j, "\"kind\":\"failure_latch\""));
  EXPECT_TRUE(Has(j, "\"journal_total\":8"));
}

TEST(SnapshotJson, NonFiniteFloatsBecomeNull) {
  BenchStateSnapshot s{};
  s.force_n = std::numeric_limits<float>::quiet_NaN();
  s.position_mm = std::numeric_limits<float>::infinity();
  const std::string j = Snapshot(s);
  EXPECT_TRUE(Has(j, "\"force_n\":null"));
  EXPECT_TRUE(Has(j, "\"position_mm\":null"));
  EXPECT_FALSE(Has(j, "nan"));
  EXPECT_FALSE(Has(j, "inf"));
}

TEST(SnapshotJson, FullestSnapshotFitsRecommendedBuffer) {
  BenchStateSnapshot s{};
  s.ack_count = BenchStateSnapshot::kAckTail;
  s.journal_count = BenchStateSnapshot::kJournalTail;
  for (auto& e : s.journal) {
    e.kind = JournalKind::kCommand;
    e.cmd_id = 4'000'000'000u;
    e.t_ms = 4'000'000'000u;
    e.seq = 4'000'000'000u;
  }
  char buf[kSnapshotJsonMaxSize];
  EXPECT_GT(BuildSnapshotJson(s, buf, sizeof(buf)), 0u);
}

TEST(SnapshotJson, TooSmallBufferReturnsZeroInsteadOfTruncating) {
  char buf[64];
  EXPECT_EQ(BuildSnapshotJson(BenchStateSnapshot{}, buf, sizeof(buf)), 0u);
  EXPECT_EQ(BuildSnapshotJson(BenchStateSnapshot{}, buf, 0), 0u);
}

TEST(AckJson, HasContractFields) {
  CommandAck a{.cmd_id = 3,
               .type = CommandType::kControlledStop,
               .result = CommandResult::kAccepted};
  char buf[256];
  const size_t n = BuildAckJson(a, buf, sizeof(buf));
  ASSERT_GT(n, 0u);
  EXPECT_EQ(
      std::string(buf, n),
      "{\"v\":1,\"type\":\"ack\",\"cmd_id\":3,\"cmd\":\"controlled_stop\","
      "\"result\":\"accepted\",\"reason\":\"none\",\"duplicate\":false}");
}

TEST(ParseCommand, SetSetpoint) {
  BenchCommand c{};
  ASSERT_EQ(ParseCommandJson(
                R"({"cmd_id":7,"type":"set_setpoint","value":-12.5})", c),
            ParseError::kOk);
  EXPECT_EQ(c.cmd_id, 7u);
  EXPECT_EQ(c.type, CommandType::kSetSetpoint);
  EXPECT_FLOAT_EQ(c.value, -12.5f);
}

TEST(ParseCommand, EnableAndModeAndStops) {
  BenchCommand c{};
  ASSERT_EQ(
      ParseCommandJson(R"({"cmd_id":1,"type":"enable","enable":true})", c),
      ParseError::kOk);
  EXPECT_EQ(c.type, CommandType::kEnable);
  EXPECT_TRUE(c.enable);

  ASSERT_EQ(
      ParseCommandJson(R"({"cmd_id":2,"type":"set_mode","mode":"force"})", c),
      ParseError::kOk);
  EXPECT_EQ(c.mode, ControlMode::kForce);

  ASSERT_EQ(ParseCommandJson(R"({"cmd_id":3,"type":"controlled_stop"})", c),
            ParseError::kOk);
  EXPECT_EQ(c.type, CommandType::kControlledStop);

  ASSERT_EQ(
      ParseCommandJson(R"(  { "cmd_id" : 4 , "type" : "e_stop_soft" }  )", c),
      ParseError::kOk);
  EXPECT_EQ(c.type, CommandType::kEStopSoft);
}

TEST(ParseCommand, RejectsMalformedAndIncomplete) {
  BenchCommand c{};
  EXPECT_EQ(ParseCommandJson("", c), ParseError::kSyntax);
  EXPECT_EQ(ParseCommandJson("[]", c), ParseError::kSyntax);
  EXPECT_EQ(ParseCommandJson(R"({"cmd_id":1,"type":"enable","enable":true)", c),
            ParseError::kSyntax);
  EXPECT_EQ(
      ParseCommandJson(R"({"cmd_id":1,"type":"enable","enable":true} x)", c),
      ParseError::kSyntax);
  EXPECT_EQ(ParseCommandJson(R"({"type":"enable","enable":true})", c),
            ParseError::kMissingField);
  EXPECT_EQ(ParseCommandJson(R"({"cmd_id":1,"type":"enable"})", c),
            ParseError::kMissingField);
  EXPECT_EQ(ParseCommandJson(R"({"cmd_id":1,"type":"set_mode"})", c),
            ParseError::kMissingField);
  EXPECT_EQ(ParseCommandJson(R"({"cmd_id":1,"type":"set_setpoint"})", c),
            ParseError::kMissingField);
}

TEST(ParseCommand, RejectsBadValuesAndUnknowns) {
  BenchCommand c{};
  EXPECT_EQ(ParseCommandJson(R"({"cmd_id":-1,"type":"controlled_stop"})", c),
            ParseError::kBadValue);
  EXPECT_EQ(ParseCommandJson(R"({"cmd_id":1.5,"type":"controlled_stop"})", c),
            ParseError::kBadValue);
  EXPECT_EQ(
      ParseCommandJson(R"({"cmd_id":1,"type":"set_setpoint","value":"5"})", c),
      ParseError::kBadValue);
  EXPECT_EQ(ParseCommandJson(R"({"cmd_id":1,"type":"enable","enable":1})", c),
            ParseError::kBadValue);
  EXPECT_EQ(ParseCommandJson(R"({"cmd_id":1,"type":"reboot"})", c),
            ParseError::kUnknownType);
  EXPECT_EQ(
      ParseCommandJson(R"({"cmd_id":1,"type":"set_mode","mode":"torque"})", c),
      ParseError::kUnknownType);
  EXPECT_EQ(ParseCommandJson(
                R"({"cmd_id":1,"type":"controlled_stop","ramp_ms":5})", c),
            ParseError::kUnknownKey);
}

TEST(ParseCommand, FailedParseLeavesOutputUntouched) {
  BenchCommand c{.cmd_id = 99};
  EXPECT_NE(ParseCommandJson(R"({"cmd_id":1})", c), ParseError::kOk);
  EXPECT_EQ(c.cmd_id, 99u);
}

}  // namespace
}  // namespace bench
