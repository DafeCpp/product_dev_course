#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <vector>

#include "vesc_protocol.hpp"

namespace vesc = rc_vehicle::vesc;

// ══════════════════════════════════════════════════════════════════════════════
// VESC UART (LOS-19): кадрирование, CRC, разбор COMM_GET_VALUES
// ══════════════════════════════════════════════════════════════════════════════

namespace {

void PutI16(std::vector<uint8_t>& v, int16_t x) {
  const auto u = static_cast<uint16_t>(x);
  v.push_back(static_cast<uint8_t>(u >> 8));
  v.push_back(static_cast<uint8_t>(u & 0xFF));
}

void PutI32(std::vector<uint8_t>& v, int32_t x) {
  const auto u = static_cast<uint32_t>(x);
  for (int shift = 24; shift >= 0; shift -= 8) {
    v.push_back(static_cast<uint8_t>((u >> shift) & 0xFF));
  }
}

/// Ответ GET_VALUES в раскладке прошивки VESC (bldc/comm/commands.c).
std::vector<uint8_t> MakeGetValues(bool with_newer_fields = false) {
  std::vector<uint8_t> p;
  p.push_back(vesc::kCommGetValues);
  PutI16(p, 412);      // temp_fet 41.2 °C
  PutI16(p, -5);       // temp_motor -0.5 °C
  PutI32(p, 1234);     // motor current 12.34 A
  PutI32(p, -250);     // input current -2.50 A (рекуперация)
  PutI32(p, 111);      // avg_id
  PutI32(p, 222);      // avg_iq
  PutI16(p, -456);     // duty -0.456
  PutI32(p, -23456);   // erpm
  PutI16(p, 118);      // v_in 11.8 V
  PutI32(p, 1);        // amp_hours
  PutI32(p, 2);        // amp_hours_charged
  PutI32(p, 3);        // watt_hours
  PutI32(p, 4);        // watt_hours_charged
  PutI32(p, 987654);   // tachometer
  PutI32(p, 1000000);  // tachometer_abs
  p.push_back(3);      // fault_code
  if (with_newer_fields) {
    PutI32(p, 0);    // pid_pos
    p.push_back(7);  // controller_id
  }
  return p;
}

std::optional<std::vector<uint8_t>> FeedAll(vesc::PacketParser& parser,
                                            const std::vector<uint8_t>& bytes) {
  std::optional<std::vector<uint8_t>> got;
  for (uint8_t b : bytes) {
    if (auto payload = parser.Feed(b)) {
      got.emplace(payload->begin(), payload->end());
    }
  }
  return got;
}

std::vector<uint8_t> Encode(const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> out(vesc::FrameSize(payload.size()));
  const size_t n = vesc::EncodePacket(payload, out);
  out.resize(n);
  return out;
}

}  // namespace

TEST(VescProtocol, Crc16MatchesXmodemCheckValue) {
  const uint8_t data[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  EXPECT_EQ(vesc::Crc16(data), 0x31C3);
}

TEST(VescProtocol, GetValuesRequestFrame) {
  uint8_t buf[8] = {};
  ASSERT_EQ(vesc::BuildGetValuesRequest(buf), 6u);
  const uint8_t id[] = {vesc::kCommGetValues};
  const uint16_t crc = vesc::Crc16(id);
  EXPECT_EQ(buf[0], 0x02);
  EXPECT_EQ(buf[1], 1);
  EXPECT_EQ(buf[2], vesc::kCommGetValues);
  EXPECT_EQ(buf[3], crc >> 8);
  EXPECT_EQ(buf[4], crc & 0xFF);
  EXPECT_EQ(buf[5], 0x03);
}

TEST(VescProtocol, EncodeRejectsBadSizes) {
  uint8_t small[4] = {};
  const uint8_t one[] = {1};
  EXPECT_EQ(vesc::EncodePacket(one, small), 0u);  // кадр 6 байт не влезает
  uint8_t out[16] = {};
  EXPECT_EQ(vesc::EncodePacket(std::span<const uint8_t>{}, out), 0u);
}

TEST(VescProtocol, ParserRoundTripWithGarbage) {
  const auto payload = MakeGetValues();
  auto bytes = Encode(payload);
  bytes.insert(bytes.begin(), {0xFF, 0x00, 0x7E});  // мусор до кадра
  vesc::PacketParser parser;
  const auto got = FeedAll(parser, bytes);
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ(*got, payload);
  EXPECT_EQ(parser.ErrorCount(), 0u);
}

TEST(VescProtocol, ParserRejectsBadCrcAndRecovers) {
  const auto payload = MakeGetValues();
  auto bad = Encode(payload);
  bad[5] ^= 0x01;  // порча байта payload
  const auto good = Encode(payload);

  vesc::PacketParser parser;
  EXPECT_FALSE(FeedAll(parser, bad).has_value());
  EXPECT_EQ(parser.ErrorCount(), 1u);
  const auto got = FeedAll(parser, good);
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ(*got, payload);
}

TEST(VescProtocol, ParserRejectsMissingTerminator) {
  auto bytes = Encode({vesc::kCommGetValues});
  bytes.back() = 0x00;
  vesc::PacketParser parser;
  EXPECT_FALSE(FeedAll(parser, bytes).has_value());
  EXPECT_EQ(parser.ErrorCount(), 1u);
}

TEST(VescProtocol, ParserRejectsOversizedLength) {
  // Длинный кадр с длиной больше буфера.
  vesc::PacketParser parser;
  EXPECT_FALSE(FeedAll(parser, {0x03, 0x10, 0x00}).has_value());
  EXPECT_EQ(parser.ErrorCount(), 1u);
}

TEST(VescProtocol, ParserAcceptsLongFrameFormat) {
  const std::vector<uint8_t> payload = {vesc::kCommGetValues, 1, 2};
  const uint16_t crc = vesc::Crc16(payload);
  std::vector<uint8_t> bytes = {0x03, 0x00, 0x03};
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  bytes.push_back(static_cast<uint8_t>(crc >> 8));
  bytes.push_back(static_cast<uint8_t>(crc & 0xFF));
  bytes.push_back(0x03);
  vesc::PacketParser parser;
  const auto got = FeedAll(parser, bytes);
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ(*got, payload);
}

TEST(VescProtocol, ParseGetValuesFields) {
  const auto t = vesc::ParseGetValues(MakeGetValues());
  ASSERT_TRUE(t.has_value());
  EXPECT_FLOAT_EQ(t->temp_fet_c, 41.2f);
  EXPECT_FLOAT_EQ(t->temp_motor_c, -0.5f);
  EXPECT_FLOAT_EQ(t->motor_current_a, 12.34f);
  EXPECT_FLOAT_EQ(t->input_current_a, -2.5f);
  EXPECT_FLOAT_EQ(t->duty, -0.456f);
  EXPECT_FLOAT_EQ(t->erpm, -23456.0f);
  EXPECT_FLOAT_EQ(t->input_voltage_v, 11.8f);
  EXPECT_EQ(t->tachometer, 987654);
  EXPECT_EQ(t->fault_code, 3);
  EXPECT_EQ(t->age_ms, 0u);
}

TEST(VescProtocol, ParseGetValuesIgnoresNewerTrailingFields) {
  const auto t = vesc::ParseGetValues(MakeGetValues(true));
  ASSERT_TRUE(t.has_value());
  EXPECT_FLOAT_EQ(t->erpm, -23456.0f);
  EXPECT_EQ(t->fault_code, 3);
}

TEST(VescProtocol, ParseGetValuesRejectsShortOrForeignPayload) {
  auto p = MakeGetValues();
  p.pop_back();  // без fault_code
  EXPECT_FALSE(vesc::ParseGetValues(p).has_value());

  auto other = MakeGetValues();
  other[0] = 0;  // COMM_FW_VERSION
  EXPECT_FALSE(vesc::ParseGetValues(other).has_value());

  EXPECT_FALSE(vesc::ParseGetValues({}).has_value());
}
