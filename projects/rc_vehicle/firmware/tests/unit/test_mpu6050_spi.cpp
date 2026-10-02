#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "mpu6050_spi.hpp"

namespace {

// Fake SpiDevice: записывает все транзакции и отдаёт заранее заданные ответы.
class FakeSpiDevice : public SpiDevice {
 public:
  int Init() override { return 0; }

  int Transfer(std::span<const uint8_t> tx, std::span<uint8_t> rx) override {
    ++transfer_count_;
    last_tx_.assign(tx.begin(), tx.end());
    if (transfer_result_ != 0)
      return transfer_result_;

    std::memset(rx.data(), 0, rx.size());
    if (!responses_.empty()) {
      const auto& resp = responses_.front();
      std::memcpy(rx.data(), resp.data(), std::min(rx.size(), resp.size()));
      responses_.erase(responses_.begin());
    }
    return 0;
  }

  void PushResponse(std::vector<uint8_t> r) { responses_.push_back(std::move(r)); }
  void SetTransferResult(int r) { transfer_result_ = r; }
  void ResetCounters() { transfer_count_ = 0; }
  int transfer_count() const { return transfer_count_; }
  const std::vector<uint8_t>& last_tx() const { return last_tx_; }

 private:
  int transfer_result_{0};
  int transfer_count_{0};
  std::vector<uint8_t> last_tx_;
  std::vector<std::vector<uint8_t>> responses_;
};

// Init: WriteReg(reset), ReadReg(WHO_AM_I) → 0x68, WriteReg(wake)
void InitOk(FakeSpiDevice& spi, Mpu6050Spi& imu) {
  spi.PushResponse({0, 0});
  spi.PushResponse({0, 0x68});
  ASSERT_EQ(imu.Init(), 0);
  spi.ResetCounters();
}

}  // namespace

TEST(Mpu6050SpiTest, ReadBeforeInitFails) {
  FakeSpiDevice spi;
  Mpu6050Spi imu(&spi);
  ImuData d;
  EXPECT_EQ(imu.Read(d), -1);
  EXPECT_EQ(spi.transfer_count(), 0);
}

TEST(Mpu6050SpiTest, ReadUsesSingleBurstTransaction) {
  FakeSpiDevice spi;
  Mpu6050Spi imu(&spi);
  InitOk(spi, imu);

  ImuData d;
  ASSERT_EQ(imu.Read(d), 0);

  EXPECT_EQ(spi.transfer_count(), 1);
  ASSERT_EQ(spi.last_tx().size(), 15u);
  EXPECT_EQ(spi.last_tx()[0], 0x3B | 0x80);
}

TEST(Mpu6050SpiTest, ReadParsesAllAxes) {
  FakeSpiDevice spi;
  Mpu6050Spi imu(&spi);
  InitOk(spi, imu);

  // rx[0] — эхо адреса; ax, ay, az, temp, gx, gy, gz (big-endian)
  spi.PushResponse({0x00,
                    0x40, 0x00,   // ax = 16384  → 1.0 g
                    0xC0, 0x00,   // ay = -16384 → -1.0 g
                    0x20, 0x00,   // az = 8192   → 0.5 g
                    0x12, 0x34,   // temp (игнорируется)
                    0x00, 0x83,   // gx = 131    → 1.0 °/s
                    0xFF, 0x7D,   // gy = -131   → -1.0 °/s
                    0x01, 0x06}); // gz = 262    → 2.0 °/s

  ImuData d;
  ASSERT_EQ(imu.Read(d), 0);
  EXPECT_FLOAT_EQ(d.ax, 1.0f);
  EXPECT_FLOAT_EQ(d.ay, -1.0f);
  EXPECT_FLOAT_EQ(d.az, 0.5f);
  EXPECT_FLOAT_EQ(d.gx, 1.0f);
  EXPECT_FLOAT_EQ(d.gy, -1.0f);
  EXPECT_FLOAT_EQ(d.gz, 2.0f);
}

TEST(Mpu6050SpiTest, ReadPropagatesTransferError) {
  FakeSpiDevice spi;
  Mpu6050Spi imu(&spi);
  InitOk(spi, imu);

  spi.SetTransferResult(-1);
  ImuData d;
  EXPECT_EQ(imu.Read(d), -1);
}
