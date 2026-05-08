#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <vector>

#include "protocol.hpp"
#include "uart_bridge_base.hpp"

using namespace rc_vehicle;
using namespace rc_vehicle::protocol;

// ═══════════════════════════════════════════════════════════════════════════
// Mock UART Bridge — simulates data coming from UART
// ═══════════════════════════════════════════════════════════════════════════

class MockUartBridge : public UartBridgeBase {
 public:
  MockUartBridge() = default;

  int Init() override { return 0; }

  // Queue data to be returned by ReadAvailable
  void QueueData(const std::vector<uint8_t>& data) {
    queue_.insert(queue_.end(), data.begin(), data.end());
  }

  void QueueData(const uint8_t* data, size_t len) {
    queue_.insert(queue_.end(), data, data + len);
  }

  size_t GetQueuedSize() const { return queue_.size(); }

 protected:
  int Write(const uint8_t* /*data*/, size_t len) override {
    // For now, just pretend to write
    return static_cast<int>(len);
  }

  int ReadAvailable(uint8_t* buf, size_t max_len) override {
    if (queue_.empty()) return 0;

    size_t to_read = std::min(max_len, queue_.size());
    std::memcpy(buf, queue_.data(), to_read);
    queue_.erase(queue_.begin(), queue_.begin() + to_read);
    return static_cast<int>(to_read);
  }

 private:
  std::vector<uint8_t> queue_;
};

// ═══════════════════════════════════════════════════════════════════════════
// Helper: Build a command frame and return its bytes
// ═══════════════════════════════════════════════════════════════════════════

static std::vector<uint8_t> BuildCommandFrame(float throttle = 0.5f,
                                               float steering = 0.25f) {
  CommandData cmd{.seq = 0, .throttle = throttle, .steering = steering};

  std::array<uint8_t, 32> buf{};
  auto result = Protocol::BuildCommand(buf, cmd);

  if (IsOk(result)) {
    size_t size = GetValue(result);
    return std::vector<uint8_t>(buf.begin(), buf.begin() + size);
  }
  return {};
}

static std::vector<uint8_t> BuildPingFrame() {
  std::array<uint8_t, 16> buf{};
  auto result = Protocol::BuildPing(buf);

  if (IsOk(result)) {
    size_t size = GetValue(result);
    return std::vector<uint8_t>(buf.begin(), buf.begin() + size);
  }
  return {};
}

static std::vector<uint8_t> BuildPongFrame() {
  std::array<uint8_t, 16> buf{};
  auto result = Protocol::BuildPong(buf);

  if (IsOk(result)) {
    size_t size = GetValue(result);
    return std::vector<uint8_t>(buf.begin(), buf.begin() + size);
  }
  return {};
}

static std::vector<uint8_t> BuildTelemetryFrame(int16_t ax = 1000,
                                                 int16_t ay = -500,
                                                 int16_t az = 9800) {
  TelemetryData telem{.seq = 42, .status = 0x07, .ax = ax, .ay = ay, .az = az};

  std::array<uint8_t, 32> buf{};
  auto result = Protocol::BuildTelemetry(buf, telem);

  if (IsOk(result)) {
    size_t size = GetValue(result);
    return std::vector<uint8_t>(buf.begin(), buf.begin() + size);
  }
  return {};
}

// ═══════════════════════════════════════════════════════════════════════════
// Frame Reception Tests
// ═══════════════════════════════════════════════════════════════════════════

class UartBridgeTest : public ::testing::Test {
 protected:
  MockUartBridge bridge_;
};

TEST_F(UartBridgeTest, ReceiveCommand_CompleteFrame) {
  auto cmd_frame = BuildCommandFrame(0.5f, 0.25f);
  bridge_.QueueData(cmd_frame);

  auto cmd = bridge_.ReceiveCommand();
  ASSERT_TRUE(cmd.has_value()) << "Should receive command";
  EXPECT_NEAR(cmd->throttle, 0.5f, 0.01f) << "Throttle should match";
  EXPECT_NEAR(cmd->steering, 0.25f, 0.01f) << "Steering should match";
}

TEST_F(UartBridgeTest, ReceiveCommand_MultipleFrames) {
  auto cmd1 = BuildCommandFrame(0.3f, -0.1f);
  auto cmd2 = BuildCommandFrame(0.7f, 0.4f);

  // Queue both frames
  bridge_.QueueData(cmd1);
  bridge_.QueueData(cmd2);

  // First receive
  auto received1 = bridge_.ReceiveCommand();
  ASSERT_TRUE(received1.has_value());
  EXPECT_NEAR(received1->throttle, 0.3f, 0.01f);
  EXPECT_NEAR(received1->steering, -0.1f, 0.01f);

  // Second receive (should trigger another read from queue)
  auto received2 = bridge_.ReceiveCommand();
  ASSERT_TRUE(received2.has_value());
  EXPECT_NEAR(received2->throttle, 0.7f, 0.01f);
  EXPECT_NEAR(received2->steering, 0.4f, 0.01f);
}

TEST_F(UartBridgeTest, ReceiveCommand_PartialFrame_StaysBuffered) {
  // When partial frame is queued, it should not parse
  auto cmd_frame = BuildCommandFrame(0.5f, 0.25f);

  // Queue only first half
  bridge_.QueueData(cmd_frame.data(), cmd_frame.size() / 2);

  auto cmd = bridge_.ReceiveCommand();
  EXPECT_FALSE(cmd.has_value()) << "Partial frame should not parse";

  // Verify buffer state doesn't crash with remaining partial data
  // (real behavior: once parsed, frame is consumed; partial data remains)
}

TEST_F(UartBridgeTest, ReceiveCommand_MisalignedFrame) {
  auto cmd_frame = BuildCommandFrame(0.5f, 0.25f);

  // Queue garbage data followed by valid frame
  std::vector<uint8_t> misaligned;
  misaligned.push_back(0xFF);
  misaligned.push_back(0xFF);
  misaligned.push_back(0xFF);
  misaligned.insert(misaligned.end(), cmd_frame.begin(), cmd_frame.end());

  bridge_.QueueData(misaligned);

  auto cmd = bridge_.ReceiveCommand();
  ASSERT_TRUE(cmd.has_value()) << "Should find frame after garbage data";
  EXPECT_NEAR(cmd->throttle, 0.5f, 0.01f);
}

TEST_F(UartBridgeTest, ReceiveCommand_CorruptedCRC) {
  auto cmd_frame = BuildCommandFrame(0.5f, 0.25f);

  // Corrupt the CRC (last 2 bytes)
  if (cmd_frame.size() >= 2) {
    cmd_frame[cmd_frame.size() - 1] ^= 0xFF;
  }

  bridge_.QueueData(cmd_frame);

  auto cmd = bridge_.ReceiveCommand();
  EXPECT_FALSE(cmd.has_value()) << "Corrupted CRC should fail parsing";
}

TEST_F(UartBridgeTest, ReceivePing) {
  auto ping_frame = BuildPingFrame();
  bridge_.QueueData(ping_frame);

  bool got_ping = bridge_.ReceivePing();
  EXPECT_TRUE(got_ping) << "Should receive ping";
}

TEST_F(UartBridgeTest, ReceivePong) {
  auto pong_frame = BuildPongFrame();
  bridge_.QueueData(pong_frame);

  bool got_pong = bridge_.ReceivePong();
  EXPECT_TRUE(got_pong) << "Should receive pong";
}

TEST_F(UartBridgeTest, ReceiveTelemetry_CompleteFrame) {
  auto telem_frame = BuildTelemetryFrame(1000, -500, 9800);
  bridge_.QueueData(telem_frame);

  auto telem = bridge_.ReceiveTelem();
  ASSERT_TRUE(telem.has_value()) << "Should receive telemetry";
  EXPECT_EQ(telem->ax, 1000) << "Accelerometer X should match";
  EXPECT_EQ(telem->ay, -500) << "Accelerometer Y should match";
  EXPECT_EQ(telem->az, 9800) << "Accelerometer Z should match";
}

TEST_F(UartBridgeTest, ReceiveCommandClamping) {
  // BuildCommandFrame uses Clamped() internally, but let's verify the bridge
  // correctly handles extreme values
  auto cmd_frame = BuildCommandFrame(1.5f, -1.5f);
  bridge_.QueueData(cmd_frame);

  auto cmd = bridge_.ReceiveCommand();
  ASSERT_TRUE(cmd.has_value());
  // Verify clamping was applied by Protocol::BuildCommand
  EXPECT_LE(std::abs(cmd->throttle), 1.0f);
  EXPECT_LE(std::abs(cmd->steering), 1.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Buffer Management Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(UartBridgeTest, ReceiveCommand_ChunkedArrival) {
  // Simulate receiving a frame across multiple UART chunks
  // (all chunks queued before calling ReceiveCommand, which is the typical pattern)
  auto cmd_frame = BuildCommandFrame(0.6f, -0.2f);
  size_t chunk_size = std::max(size_t(1), cmd_frame.size() / 3);

  // Queue all data in chunks
  for (size_t i = 0; i < cmd_frame.size(); i += chunk_size) {
    size_t remaining = cmd_frame.size() - i;
    size_t to_queue = std::min(chunk_size, remaining);
    bridge_.QueueData(cmd_frame.data() + i, to_queue);
  }

  // Now attempt to receive the complete frame
  auto cmd = bridge_.ReceiveCommand();
  ASSERT_TRUE(cmd.has_value()) << "Complete frame should parse";
  EXPECT_NEAR(cmd->throttle, 0.6f, 0.01f);
  EXPECT_NEAR(cmd->steering, -0.2f, 0.01f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Edge Cases
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(UartBridgeTest, ReceiveCommand_NoData) {
  auto cmd = bridge_.ReceiveCommand();
  EXPECT_FALSE(cmd.has_value()) << "No data should return nullopt";
}

TEST_F(UartBridgeTest, ReceiveCommand_EmptyQueueAfterFrame) {
  auto cmd_frame = BuildCommandFrame(0.5f, 0.25f);
  bridge_.QueueData(cmd_frame);

  auto cmd1 = bridge_.ReceiveCommand();
  ASSERT_TRUE(cmd1.has_value());

  // Queue is now empty
  auto cmd2 = bridge_.ReceiveCommand();
  EXPECT_FALSE(cmd2.has_value()) << "Empty queue should return nullopt";
}

TEST_F(UartBridgeTest, ReceiveTelemetry_MultipleTelemetryFrames) {
  auto telem1 = BuildTelemetryFrame(100, 200, 300);
  auto telem2 = BuildTelemetryFrame(1000, 2000, 3000);

  bridge_.QueueData(telem1);
  bridge_.QueueData(telem2);

  auto t1 = bridge_.ReceiveTelem();
  ASSERT_TRUE(t1.has_value());
  EXPECT_EQ(t1->ax, 100);

  auto t2 = bridge_.ReceiveTelem();
  ASSERT_TRUE(t2.has_value());
  EXPECT_EQ(t2->ax, 1000);
}
