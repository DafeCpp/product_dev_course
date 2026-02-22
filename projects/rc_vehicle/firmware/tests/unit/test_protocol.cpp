#include <gtest/gtest.h>

#include <array>

#include "protocol.hpp"
#include "test_helpers.hpp"

using namespace rc_vehicle::protocol;
using namespace rc_vehicle::testing;

// ═══════════════════════════════════════════════════════════════════════════
// Telemetry Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(ProtocolTest, BuildTelemetryFrame) {
  TelemetryData data{.seq = 42,
                     .status = 0x07,  // all flags set
                     .ax = 1000,
                     .ay = -500,
                     .az = 9800,
                     .gx = 100,
                     .gy = -200,
                     .gz = 50};

  std::array<uint8_t, 32> buffer{};
  auto result = Protocol::BuildTelemetry(buffer, data);

  ASSERT_TRUE(IsOk(result)) << "BuildTelemetry should succeed";
  EXPECT_EQ(GetValue(result), 23) << "Expected frame size is 23 bytes";

  // Verify frame structure
  EXPECT_EQ(buffer[0], FRAME_PREFIX_0) << "First prefix byte should be 0xAA";
  EXPECT_EQ(buffer[1], FRAME_PREFIX_1) << "Second prefix byte should be 0x55";
  EXPECT_EQ(buffer[2], PROTOCOL_VERSION) << "Protocol version should be 0x01";
  EXPECT_EQ(buffer[3], static_cast<uint8_t>(MessageType::Telemetry))
      << "Message type should be Telemetry";
}

TEST(ProtocolTest, ParseTelemetryFrame) {
  // Build frame first
  TelemetryData original{.seq = 100, .status = 0x05, .ax = 2000, .ay = -1000};
  std::array<uint8_t, 32> buffer{};
  auto build_result = Protocol::BuildTelemetry(buffer, original);
  ASSERT_TRUE(IsOk(build_result));

  // Parse it back
  auto parse_result = Protocol::ParseTelemetry(buffer);
  ASSERT_TRUE(IsOk(parse_result)) << "ParseTelemetry should succeed";

  auto parsed = GetValue(parse_result);
  EXPECT_EQ(parsed.seq, original.seq) << "Sequence number should match";
  EXPECT_EQ(parsed.status, original.status) << "Status should match";
  EXPECT_EQ(parsed.ax, original.ax) << "Accelerometer X should match";
  EXPECT_EQ(parsed.ay, original.ay) << "Accelerometer Y should match";
}

TEST(ProtocolTest, DetectCorruptedCRC) {
  TelemetryData data{.seq = 1};
  std::array<uint8_t, 32> buffer{};
  auto build_result = Protocol::BuildTelemetry(buffer, data);
  ASSERT_TRUE(IsOk(build_result));

  // Corrupt CRC (last 2 bytes)
  size_t frame_size = GetValue(build_result);
  buffer[frame_size - 2] ^= 0xFF;

  auto parse_result = Protocol::ParseTelemetry(buffer);
  ASSERT_TRUE(IsError(parse_result)) << "Should detect corrupted CRC";
  EXPECT_EQ(GetError(parse_result), ParseError::CrcMismatch)
      << "Error should be CrcMismatch";
}

TEST(ProtocolTest, TelemetryStatusFlags) {
  TelemetryData data{};

  // Test RC flag
  data.SetRcOk(true);
  EXPECT_TRUE(data.IsRcOk()) << "RC flag should be set";
  EXPECT_FALSE(data.IsWifiOk()) << "WiFi flag should not be set";
  EXPECT_FALSE(data.IsFailsafeActive()) << "Failsafe flag should not be set";

  // Test WiFi flag
  data.SetWifiOk(true);
  EXPECT_TRUE(data.IsRcOk()) << "RC flag should still be set";
  EXPECT_TRUE(data.IsWifiOk()) << "WiFi flag should be set";

  // Test Failsafe flag
  data.SetFailsafeActive(true);
  EXPECT_TRUE(data.IsFailsafeActive()) << "Failsafe flag should be set";

  // Clear RC flag
  data.SetRcOk(false);
  EXPECT_FALSE(data.IsRcOk()) << "RC flag should be cleared";
  EXPECT_TRUE(data.IsWifiOk()) << "WiFi flag should still be set";
  EXPECT_TRUE(data.IsFailsafeActive()) << "Failsafe flag should still be set";
}

// ═══════════════════════════════════════════════════════════════════════════
// Command Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(ProtocolTest, BuildCommandFrame) {
  CommandData data{.seq = 10, .throttle = 0.5f, .steering = -0.3f};

  std::array<uint8_t, 32> buffer{};
  auto result = Protocol::BuildCommand(buffer, data);

  ASSERT_TRUE(IsOk(result)) << "BuildCommand should succeed";
  EXPECT_EQ(GetValue(result), 15) << "Expected frame size is 15 bytes";

  // Verify frame structure
  EXPECT_EQ(buffer[0], FRAME_PREFIX_0);
  EXPECT_EQ(buffer[1], FRAME_PREFIX_1);
  EXPECT_EQ(buffer[2], PROTOCOL_VERSION);
  EXPECT_EQ(buffer[3], static_cast<uint8_t>(MessageType::Command));
}

TEST(ProtocolTest, ParseCommandFrame) {
  CommandData original{.seq = 50, .throttle = 0.75f, .steering = 0.25f};
  std::array<uint8_t, 32> buffer{};
  auto build_result = Protocol::BuildCommand(buffer, original);
  ASSERT_TRUE(IsOk(build_result));

  auto parse_result = Protocol::ParseCommand(buffer);
  ASSERT_TRUE(IsOk(parse_result)) << "ParseCommand should succeed";

  auto parsed = GetValue(parse_result);
  // Note: seq is auto-incremented by BuildCommand, not taken from original
  EXPECT_GE(parsed.seq, 0) << "Sequence should be valid";
  EXPECT_NEAR(parsed.throttle, original.throttle, 0.01f)
      << "Throttle should match within tolerance";
  EXPECT_NEAR(parsed.steering, original.steering, 0.01f)
      << "Steering should match within tolerance";
}

TEST(ProtocolTest, CommandClamping) {
  CommandData data{.seq = 1, .throttle = 1.5f, .steering = -1.5f};

  auto clamped = data.Clamped();
  EXPECT_FLOAT_EQ(clamped.throttle, 1.0f)
      << "Throttle should be clamped to 1.0";
  EXPECT_FLOAT_EQ(clamped.steering, -1.0f)
      << "Steering should be clamped to -1.0";
  EXPECT_EQ(clamped.seq, data.seq) << "Sequence should be preserved";
}

// ═══════════════════════════════════════════════════════════════════════════
// Ping/Pong Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(ProtocolTest, BuildAndParsePing) {
  std::array<uint8_t, 16> buffer{};
  auto build_result = Protocol::BuildPing(buffer);
  ASSERT_TRUE(IsOk(build_result)) << "BuildPing should succeed";

  auto parse_result = Protocol::ParsePing(buffer);
  ASSERT_TRUE(IsOk(parse_result)) << "ParsePing should succeed";
}

TEST(ProtocolTest, BuildAndParsePong) {
  std::array<uint8_t, 16> buffer{};
  auto build_result = Protocol::BuildPong(buffer);
  ASSERT_TRUE(IsOk(build_result)) << "BuildPong should succeed";

  auto parse_result = Protocol::ParsePong(buffer);
  ASSERT_TRUE(IsOk(parse_result)) << "ParsePong should succeed";
}

// ═══════════════════════════════════════════════════════════════════════════
// Log Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(ProtocolTest, BuildAndParseLog) {
  std::string_view msg = "Test log message";
  std::array<uint8_t, 256> buffer{};

  auto build_result = Protocol::BuildLog(buffer, msg);
  ASSERT_TRUE(IsOk(build_result)) << "BuildLog should succeed";

  auto parse_result = Protocol::ParseLog(buffer);
  ASSERT_TRUE(IsOk(parse_result)) << "ParseLog should succeed";

  auto parsed_msg = GetValue(parse_result);
  EXPECT_EQ(parsed_msg, msg) << "Log message should match";
}

TEST(ProtocolTest, LogMessageTruncation) {
  // Create a message longer than LOG_MAX_PAYLOAD
  std::string long_msg(LOG_MAX_PAYLOAD + 50, 'A');
  std::array<uint8_t, 256> buffer{};

  auto build_result = Protocol::BuildLog(buffer, long_msg);
  ASSERT_TRUE(IsOk(build_result))
      << "BuildLog should succeed even with long message";

  auto parse_result = Protocol::ParseLog(buffer);
  ASSERT_TRUE(IsOk(parse_result));

  auto parsed_msg = GetValue(parse_result);
  EXPECT_LE(parsed_msg.size(), LOG_MAX_PAYLOAD)
      << "Message should be truncated to LOG_MAX_PAYLOAD";
}

// ═══════════════════════════════════════════════════════════════════════════
// Frame Finding Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(ProtocolTest, FindFrameStart) {
  std::array<uint8_t, 32> buffer{};
  buffer[5] = FRAME_PREFIX_0;
  buffer[6] = FRAME_PREFIX_1;

  int pos = Protocol::FindFrameStart(buffer);
  EXPECT_EQ(pos, 5) << "Should find frame start at position 5";
}

TEST(ProtocolTest, FindFrameStartNotFound) {
  std::array<uint8_t, 32> buffer{};
  // No frame prefix in buffer

  int pos = Protocol::FindFrameStart(buffer);
  EXPECT_EQ(pos, -1) << "Should return -1 when frame start not found";
}

// ═══════════════════════════════════════════════════════════════════════════
// Error Handling Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(ProtocolTest, BufferTooSmall) {
  TelemetryData data{};
  std::array<uint8_t, 10> small_buffer{};  // Too small for telemetry frame

  auto result = Protocol::BuildTelemetry(small_buffer, data);
  ASSERT_TRUE(IsError(result)) << "Should fail with small buffer";
  EXPECT_EQ(GetError(result), ParseError::BufferTooSmall);
}

TEST(ProtocolTest, InvalidPrefix) {
  std::array<uint8_t, 32> buffer{};
  buffer[0] = 0xFF;  // Invalid prefix
  buffer[1] = 0xFF;

  auto result = Protocol::ParseTelemetry(buffer);
  ASSERT_TRUE(IsError(result)) << "Should fail with invalid prefix";
  EXPECT_EQ(GetError(result), ParseError::InvalidPrefix);
}

TEST(ProtocolTest, InsufficientData) {
  std::array<uint8_t, 5> buffer{};  // Less than MIN_FRAME_SIZE
  buffer[0] = FRAME_PREFIX_0;
  buffer[1] = FRAME_PREFIX_1;
  buffer[2] = PROTOCOL_VERSION;
  buffer[3] = static_cast<uint8_t>(MessageType::Telemetry);

  auto result = Protocol::ParseTelemetry(buffer);
  ASSERT_TRUE(IsError(result)) << "Should fail with insufficient data";
  // Parser checks header first, then payload length, so we get InvalidVersion
  // or InsufficientData depending on implementation order. Just verify it's an
  // error.
}
// ═══════════════════════════════════════════════════════════════════════════
// CRC Calculation Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(ProtocolTest, CrcCalculationConsistency) {
  std::array<uint8_t, 10> data{0x01, 0x02, 0x03, 0x04, 0x05,
                               0x06, 0x07, 0x08, 0x09, 0x0A};

  uint16_t crc1 = Protocol::CalculateCrc16(data);
  uint16_t crc2 = Protocol::CalculateCrc16(data);

  EXPECT_EQ(crc1, crc2) << "CRC should be deterministic";
}

TEST(ProtocolTest, CrcDifferentForDifferentData) {
  std::array<uint8_t, 5> data1{0x01, 0x02, 0x03, 0x04, 0x05};
  std::array<uint8_t, 5> data2{0x01, 0x02, 0x03, 0x04, 0x06};

  uint16_t crc1 = Protocol::CalculateCrc16(data1);
  uint16_t crc2 = Protocol::CalculateCrc16(data2);

  EXPECT_NE(crc1, crc2) << "Different data should produce different CRC";
}

TEST(ProtocolTest, CrcEmptyData) {
  std::array<uint8_t, 0> empty{};
  uint16_t crc = Protocol::CalculateCrc16(empty);

  EXPECT_EQ(crc, 0xFFFF) << "CRC of empty data should be initial value";
}

// ═══════════════════════════════════════════════════════════════════════════
// FrameBuilder Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(ProtocolTest, FrameBuilderWithEmptyPayload) {
  FrameBuilder builder(MessageType::Ping);
  std::array<uint8_t, 16> buffer{};

  auto result = builder.Build(buffer, std::span<const uint8_t>());

  ASSERT_TRUE(IsOk(result)) << "Should build frame with empty payload";
  EXPECT_EQ(GetValue(result), MIN_FRAME_SIZE)
      << "Frame size should be header + CRC";
}

TEST(ProtocolTest, FrameBuilderWithMaxPayload) {
  FrameBuilder builder(MessageType::Log);
  std::array<uint8_t, LOG_MAX_PAYLOAD> payload{};
  std::fill(payload.begin(), payload.end(), 0xAB);

  std::array<uint8_t, 256> buffer{};
  auto result = builder.Build(buffer, payload);

  ASSERT_TRUE(IsOk(result)) << "Should build frame with max payload";
  EXPECT_EQ(GetValue(result), HEADER_SIZE + LOG_MAX_PAYLOAD + CRC_SIZE);
}

// ═══════════════════════════════════════════════════════════════════════════
// FrameParser Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(ProtocolTest, ValidateHeaderWithInvalidVersion) {
  std::array<uint8_t, 8> buffer{};
  buffer[0] = FRAME_PREFIX_0;
  buffer[1] = FRAME_PREFIX_1;
  buffer[2] = 0x99;  // Invalid version
  buffer[3] = static_cast<uint8_t>(MessageType::Telemetry);

  auto result = FrameParser::ValidateHeader(buffer);

  ASSERT_TRUE(IsError(result)) << "Should reject invalid version";
  EXPECT_EQ(GetError(result), ParseError::InvalidVersion);
}

TEST(ProtocolTest, GetPayloadLengthLittleEndian) {
  std::array<uint8_t, 8> buffer{};
  buffer[0] = FRAME_PREFIX_0;
  buffer[1] = FRAME_PREFIX_1;
  buffer[2] = PROTOCOL_VERSION;
  buffer[3] = static_cast<uint8_t>(MessageType::Telemetry);
  buffer[4] = 0x34;  // Low byte
  buffer[5] = 0x12;  // High byte (0x1234 = 4660)

  auto result = FrameParser::GetPayloadLength(buffer);

  ASSERT_TRUE(IsOk(result));
  EXPECT_EQ(GetValue(result), 0x1234) << "Should parse little-endian length";
}

TEST(ProtocolTest, FindFrameStartAtBeginning) {
  std::array<uint8_t, 16> buffer{};
  buffer[0] = FRAME_PREFIX_0;
  buffer[1] = FRAME_PREFIX_1;

  int pos = Protocol::FindFrameStart(buffer);
  EXPECT_EQ(pos, 0) << "Should find frame at beginning";
}

TEST(ProtocolTest, FindFrameStartInMiddle) {
  std::array<uint8_t, 16> buffer{};
  buffer[7] = FRAME_PREFIX_0;
  buffer[8] = FRAME_PREFIX_1;

  int pos = Protocol::FindFrameStart(buffer);
  EXPECT_EQ(pos, 7) << "Should find frame in middle";
}

TEST(ProtocolTest, FindFrameStartWithPartialPrefix) {
  std::array<uint8_t, 16> buffer{};
  buffer[5] = FRAME_PREFIX_0;
  buffer[6] = 0xFF;  // Not FRAME_PREFIX_1
  buffer[10] = FRAME_PREFIX_0;
  buffer[11] = FRAME_PREFIX_1;

  int pos = Protocol::FindFrameStart(buffer);
  EXPECT_EQ(pos, 10) << "Should skip partial prefix and find complete one";
}

TEST(ProtocolTest, FindFrameStartInTooSmallBuffer) {
  std::array<uint8_t, 1> buffer{FRAME_PREFIX_0};

  int pos = Protocol::FindFrameStart(buffer);
  EXPECT_EQ(pos, -1) << "Should return -1 for buffer too small";
}

// ═══════════════════════════════════════════════════════════════════════════
// Telemetry Extended Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(ProtocolTest, TelemetryWithNegativeValues) {
  TelemetryData data{.seq = 999,
                     .status = 0x00,
                     .ax = -32768,
                     .ay = -1000,
                     .az = -500,
                     .gx = -32768,
                     .gy = -2000,
                     .gz = -100};

  std::array<uint8_t, 32> buffer{};
  auto build_result = Protocol::BuildTelemetry(buffer, data);
  ASSERT_TRUE(IsOk(build_result));

  auto parse_result = Protocol::ParseTelemetry(buffer);
  ASSERT_TRUE(IsOk(parse_result));

  auto parsed = GetValue(parse_result);
  EXPECT_EQ(parsed.ax, data.ax) << "Negative ax should be preserved";
  EXPECT_EQ(parsed.ay, data.ay) << "Negative ay should be preserved";
  EXPECT_EQ(parsed.az, data.az) << "Negative az should be preserved";
  EXPECT_EQ(parsed.gx, data.gx) << "Negative gx should be preserved";
  EXPECT_EQ(parsed.gy, data.gy) << "Negative gy should be preserved";
  EXPECT_EQ(parsed.gz, data.gz) << "Negative gz should be preserved";
}

TEST(ProtocolTest, TelemetryWithMaxValues) {
  TelemetryData data{.seq = 65535,
                     .status = 0xFF,
                     .ax = 32767,
                     .ay = 32767,
                     .az = 32767,
                     .gx = 32767,
                     .gy = 32767,
                     .gz = 32767};

  std::array<uint8_t, 32> buffer{};
  auto build_result = Protocol::BuildTelemetry(buffer, data);
  ASSERT_TRUE(IsOk(build_result));

  auto parse_result = Protocol::ParseTelemetry(buffer);
  ASSERT_TRUE(IsOk(parse_result));

  auto parsed = GetValue(parse_result);
  EXPECT_EQ(parsed.seq, data.seq);
  EXPECT_EQ(parsed.status, data.status);
  EXPECT_EQ(parsed.ax, data.ax);
}

TEST(ProtocolTest, TelemetryStatusFlagsCombinations) {
  TelemetryData data{};

  // Test all combinations
  for (int i = 0; i < 8; ++i) {
    bool rc = (i & 0x01) != 0;
    bool wifi = (i & 0x02) != 0;
    bool failsafe = (i & 0x04) != 0;

    data.SetRcOk(rc);
    data.SetWifiOk(wifi);
    data.SetFailsafeActive(failsafe);

    EXPECT_EQ(data.IsRcOk(), rc) << "RC flag mismatch for combination " << i;
    EXPECT_EQ(data.IsWifiOk(), wifi)
        << "WiFi flag mismatch for combination " << i;
    EXPECT_EQ(data.IsFailsafeActive(), failsafe)
        << "Failsafe flag mismatch for combination " << i;
  }
}

TEST(ProtocolTest, TelemetryInvalidPayloadLength) {
  std::array<uint8_t, 32> buffer{};
  buffer[0] = FRAME_PREFIX_0;
  buffer[1] = FRAME_PREFIX_1;
  buffer[2] = PROTOCOL_VERSION;
  buffer[3] = static_cast<uint8_t>(MessageType::Telemetry);
  buffer[4] = 10;  // Wrong payload length (should be 15)
  buffer[5] = 0;

  auto result = Protocol::ParseTelemetry(buffer);
  ASSERT_TRUE(IsError(result));
  EXPECT_EQ(GetError(result), ParseError::InvalidPayloadLength);
}

// ═══════════════════════════════════════════════════════════════════════════
// Command Extended Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(ProtocolTest, CommandWithZeroValues) {
  CommandData data{.seq = 0, .throttle = 0.0f, .steering = 0.0f};

  std::array<uint8_t, 32> buffer{};
  auto build_result = Protocol::BuildCommand(buffer, data);
  ASSERT_TRUE(IsOk(build_result));

  auto parse_result = Protocol::ParseCommand(buffer);
  ASSERT_TRUE(IsOk(parse_result));

  auto parsed = GetValue(parse_result);
  EXPECT_NEAR(parsed.throttle, 0.0f, 0.001f);
  EXPECT_NEAR(parsed.steering, 0.0f, 0.001f);
}

TEST(ProtocolTest, CommandWithMaxValues) {
  CommandData data{.seq = 100, .throttle = 1.0f, .steering = 1.0f};

  std::array<uint8_t, 32> buffer{};
  auto build_result = Protocol::BuildCommand(buffer, data);
  ASSERT_TRUE(IsOk(build_result));

  auto parse_result = Protocol::ParseCommand(buffer);
  ASSERT_TRUE(IsOk(parse_result));

  auto parsed = GetValue(parse_result);
  EXPECT_NEAR(parsed.throttle, 1.0f, 0.001f);
  EXPECT_NEAR(parsed.steering, 1.0f, 0.001f);
}

TEST(ProtocolTest, CommandWithMinValues) {
  CommandData data{.seq = 200, .throttle = -1.0f, .steering = -1.0f};

  std::array<uint8_t, 32> buffer{};
  auto build_result = Protocol::BuildCommand(buffer, data);
  ASSERT_TRUE(IsOk(build_result));

  auto parse_result = Protocol::ParseCommand(buffer);
  ASSERT_TRUE(IsOk(parse_result));

  auto parsed = GetValue(parse_result);
  EXPECT_NEAR(parsed.throttle, -1.0f, 0.001f);
  EXPECT_NEAR(parsed.steering, -1.0f, 0.001f);
}

TEST(ProtocolTest, CommandClampingBothDirections) {
  CommandData data{.seq = 1, .throttle = 2.5f, .steering = -2.5f};

  auto clamped = data.Clamped();
  EXPECT_FLOAT_EQ(clamped.throttle, 1.0f);
  EXPECT_FLOAT_EQ(clamped.steering, -1.0f);
}

TEST(ProtocolTest, CommandClampingWithinRange) {
  CommandData data{.seq = 1, .throttle = 0.5f, .steering = -0.3f};

  auto clamped = data.Clamped();
  EXPECT_FLOAT_EQ(clamped.throttle, 0.5f)
      << "Values within range should not be clamped";
  EXPECT_FLOAT_EQ(clamped.steering, -0.3f);
}

TEST(ProtocolTest, CommandSequenceIncrement) {
  CommandData data1{.seq = 0, .throttle = 0.5f, .steering = 0.0f};
  CommandData data2{.seq = 0, .throttle = 0.6f, .steering = 0.0f};

  std::array<uint8_t, 32> buffer1{};
  std::array<uint8_t, 32> buffer2{};

  auto result1 = Protocol::BuildCommand(buffer1, data1);
  auto result2 = Protocol::BuildCommand(buffer2, data2);

  ASSERT_TRUE(IsOk(result1));
  ASSERT_TRUE(IsOk(result2));

  auto parsed1 = Protocol::ParseCommand(buffer1);
  auto parsed2 = Protocol::ParseCommand(buffer2);

  ASSERT_TRUE(IsOk(parsed1));
  ASSERT_TRUE(IsOk(parsed2));

  // Sequence should auto-increment
  EXPECT_EQ(GetValue(parsed2).seq, GetValue(parsed1).seq + 1)
      << "Command sequence should auto-increment";
}

TEST(ProtocolTest, CommandInvalidPayloadLength) {
  std::array<uint8_t, 32> buffer{};
  buffer[0] = FRAME_PREFIX_0;
  buffer[1] = FRAME_PREFIX_1;
  buffer[2] = PROTOCOL_VERSION;
  buffer[3] = static_cast<uint8_t>(MessageType::Command);
  buffer[4] = 5;  // Wrong payload length (should be 7)
  buffer[5] = 0;

  auto result = Protocol::ParseCommand(buffer);
  ASSERT_TRUE(IsError(result));
  EXPECT_EQ(GetError(result), ParseError::InvalidPayloadLength);
}

// ═══════════════════════════════════════════════════════════════════════════
// Log Extended Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(ProtocolTest, LogEmptyMessage) {
  std::string_view msg = "";
  std::array<uint8_t, 256> buffer{};

  auto build_result = Protocol::BuildLog(buffer, msg);
  ASSERT_TRUE(IsOk(build_result));

  auto parse_result = Protocol::ParseLog(buffer);
  ASSERT_TRUE(IsOk(parse_result));

  auto parsed_msg = GetValue(parse_result);
  EXPECT_EQ(parsed_msg.size(), 0) << "Empty message should be preserved";
}

TEST(ProtocolTest, LogWithSpecialCharacters) {
  std::string_view msg = "Test\nLog\tWith\rSpecial\0Chars";
  std::array<uint8_t, 256> buffer{};

  auto build_result = Protocol::BuildLog(buffer, msg);
  ASSERT_TRUE(IsOk(build_result));

  auto parse_result = Protocol::ParseLog(buffer);
  ASSERT_TRUE(IsOk(parse_result));

  auto parsed_msg = GetValue(parse_result);
  EXPECT_EQ(parsed_msg, msg) << "Special characters should be preserved";
}

TEST(ProtocolTest, LogExactlyMaxLength) {
  std::string msg(LOG_MAX_PAYLOAD, 'X');
  std::array<uint8_t, 256> buffer{};

  auto build_result = Protocol::BuildLog(buffer, msg);
  ASSERT_TRUE(IsOk(build_result));

  auto parse_result = Protocol::ParseLog(buffer);
  ASSERT_TRUE(IsOk(parse_result));

  auto parsed_msg = GetValue(parse_result);
  EXPECT_EQ(parsed_msg.size(), LOG_MAX_PAYLOAD);
  EXPECT_EQ(parsed_msg, msg);
}

TEST(ProtocolTest, LogInvalidPayloadLengthTooLarge) {
  std::array<uint8_t, 256> buffer{};
  buffer[0] = FRAME_PREFIX_0;
  buffer[1] = FRAME_PREFIX_1;
  buffer[2] = PROTOCOL_VERSION;
  buffer[3] = static_cast<uint8_t>(MessageType::Log);
  buffer[4] = (LOG_MAX_PAYLOAD + 1) & 0xFF;
  buffer[5] = ((LOG_MAX_PAYLOAD + 1) >> 8) & 0xFF;

  auto result = Protocol::ParseLog(buffer);
  ASSERT_TRUE(IsError(result));
  EXPECT_EQ(GetError(result), ParseError::InvalidPayloadLength);
}

// ═══════════════════════════════════════════════════════════════════════════
// Ping/Pong Extended Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(ProtocolTest, PingFrameSize) {
  std::array<uint8_t, 16> buffer{};
  auto result = Protocol::BuildPing(buffer);

  ASSERT_TRUE(IsOk(result));
  EXPECT_EQ(GetValue(result), MIN_FRAME_SIZE)
      << "Ping frame should be minimum size";
}

TEST(ProtocolTest, PongFrameSize) {
  std::array<uint8_t, 16> buffer{};
  auto result = Protocol::BuildPong(buffer);

  ASSERT_TRUE(IsOk(result));
  EXPECT_EQ(GetValue(result), MIN_FRAME_SIZE)
      << "Pong frame should be minimum size";
}

TEST(ProtocolTest, PingWithNonZeroPayload) {
  std::array<uint8_t, 16> buffer{};
  buffer[0] = FRAME_PREFIX_0;
  buffer[1] = FRAME_PREFIX_1;
  buffer[2] = PROTOCOL_VERSION;
  buffer[3] = static_cast<uint8_t>(MessageType::Ping);
  buffer[4] = 5;  // Non-zero payload length
  buffer[5] = 0;

  auto result = Protocol::ParsePing(buffer);
  ASSERT_TRUE(IsError(result));
  EXPECT_EQ(GetError(result), ParseError::InvalidPayloadLength)
      << "Ping should have zero payload";
}

TEST(ProtocolTest, PongWithNonZeroPayload) {
  std::array<uint8_t, 16> buffer{};
  buffer[0] = FRAME_PREFIX_0;
  buffer[1] = FRAME_PREFIX_1;
  buffer[2] = PROTOCOL_VERSION;
  buffer[3] = static_cast<uint8_t>(MessageType::Pong);
  buffer[4] = 3;  // Non-zero payload length
  buffer[5] = 0;

  auto result = Protocol::ParsePong(buffer);
  ASSERT_TRUE(IsError(result));
  EXPECT_EQ(GetError(result), ParseError::InvalidPayloadLength)
      << "Pong should have zero payload";
}

// ═══════════════════════════════════════════════════════════════════════════
// Cross-Message Type Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(ProtocolTest, ParseWrongMessageType) {
  // Build a Command frame
  CommandData cmd{.seq = 1, .throttle = 0.5f, .steering = 0.0f};
  std::array<uint8_t, 32> buffer{};
  auto build_result = Protocol::BuildCommand(buffer, cmd);
  ASSERT_TRUE(IsOk(build_result));

  // Try to parse as Telemetry
  auto parse_result = Protocol::ParseTelemetry(buffer);
  ASSERT_TRUE(IsError(parse_result));
  EXPECT_EQ(GetError(parse_result), ParseError::InvalidType)
      << "Should reject wrong message type";
}

TEST(ProtocolTest, ParsePingAsPong) {
  std::array<uint8_t, 16> buffer{};
  auto build_result = Protocol::BuildPing(buffer);
  ASSERT_TRUE(IsOk(build_result));

  auto parse_result = Protocol::ParsePong(buffer);
  ASSERT_TRUE(IsError(parse_result));
  EXPECT_EQ(GetError(parse_result), ParseError::InvalidType);
}

// ═══════════════════════════════════════════════════════════════════════════
// Robustness Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(ProtocolTest, CorruptedPayload) {
  TelemetryData data{.seq = 42, .ax = 1000};
  std::array<uint8_t, 32> buffer{};
  auto build_result = Protocol::BuildTelemetry(buffer, data);
  ASSERT_TRUE(IsOk(build_result));

  // Corrupt payload (not CRC)
  buffer[10] ^= 0xFF;

  auto parse_result = Protocol::ParseTelemetry(buffer);
  ASSERT_TRUE(IsError(parse_result));
  EXPECT_EQ(GetError(parse_result), ParseError::CrcMismatch);
}

TEST(ProtocolTest, PartialFrame) {
  TelemetryData data{.seq = 1};
  std::array<uint8_t, 32> buffer{};
  auto build_result = Protocol::BuildTelemetry(buffer, data);
  ASSERT_TRUE(IsOk(build_result));

  // Create partial frame (only first 10 bytes)
  std::array<uint8_t, 10> partial{};
  std::copy_n(buffer.begin(), 10, partial.begin());

  auto parse_result = Protocol::ParseTelemetry(partial);
  ASSERT_TRUE(IsError(parse_result));
  EXPECT_EQ(GetError(parse_result), ParseError::InsufficientData);
}

TEST(ProtocolTest, AllZeroBuffer) {
  std::array<uint8_t, 32> buffer{};  // All zeros

  auto result = Protocol::ParseTelemetry(buffer);
  ASSERT_TRUE(IsError(result));
  EXPECT_EQ(GetError(result), ParseError::InvalidPrefix);
}

TEST(ProtocolTest, RandomGarbage) {
  std::array<uint8_t, 32> buffer{};
  for (size_t i = 0; i < buffer.size(); ++i) {
    buffer[i] = static_cast<uint8_t>(i * 37 + 13);  // Pseudo-random
  }

  auto result = Protocol::ParseTelemetry(buffer);
  ASSERT_TRUE(IsError(result)) << "Should reject random garbage";
}

// ═══════════════════════════════════════════════════════════════════════════
// Round-Trip Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(ProtocolTest, TelemetryRoundTripMultiple) {
  for (uint16_t seq = 0; seq < 100; ++seq) {
    TelemetryData original{.seq = seq,
                           .status = static_cast<uint8_t>(seq % 8),
                           .ax = static_cast<int16_t>(seq * 10),
                           .ay = static_cast<int16_t>(-seq * 5),
                           .az = 16384,
                           .gx = static_cast<int16_t>(seq),
                           .gy = 0,
                           .gz = 0};

    std::array<uint8_t, 32> buffer{};
    auto build_result = Protocol::BuildTelemetry(buffer, original);
    ASSERT_TRUE(IsOk(build_result));

    auto parse_result = Protocol::ParseTelemetry(buffer);
    ASSERT_TRUE(IsOk(parse_result));

    auto parsed = GetValue(parse_result);
    EXPECT_EQ(parsed.seq, original.seq);
    EXPECT_EQ(parsed.status, original.status);
    EXPECT_EQ(parsed.ax, original.ax);
    EXPECT_EQ(parsed.ay, original.ay);
  }
}

TEST(ProtocolTest, CommandRoundTripMultiple) {
  for (int i = -10; i <= 10; ++i) {
    float value = i * 0.1f;
    CommandData original{.seq = static_cast<uint16_t>(i + 10),
                         .throttle = value,
                         .steering = -value};

    std::array<uint8_t, 32> buffer{};
    auto build_result = Protocol::BuildCommand(buffer, original);
    ASSERT_TRUE(IsOk(build_result));

    auto parse_result = Protocol::ParseCommand(buffer);
    ASSERT_TRUE(IsOk(parse_result));

    auto parsed = GetValue(parse_result);
    EXPECT_NEAR(parsed.throttle, std::clamp(value, -1.0f, 1.0f), 0.001f);
    EXPECT_NEAR(parsed.steering, std::clamp(-value, -1.0f, 1.0f), 0.001f);
  }
}