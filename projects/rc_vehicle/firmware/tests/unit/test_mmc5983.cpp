#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <cmath>
#include <cstring>
#include <vector>

#include "mmc5983_spi.hpp"

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArrayArgument;
using ::testing::Invoke;

// ─────────────────────────────────────────────────────────────────────────────
// Fake SpiDevice: програмируемый транспорт для тестов
// ─────────────────────────────────────────────────────────────────────────────

class FakeSpiDevice : public SpiDevice {
 public:
  int Init() override { return init_result_; }

  int Transfer(std::span<const uint8_t> tx, std::span<uint8_t> rx) override {
    ++transfer_count_;

    if (transfer_result_ != 0)
      return transfer_result_;

    // Запись входящих данных
    if (!tx.empty())
      last_tx_.assign(tx.begin(), tx.end());

    // Подготовка ответа: берём из очереди responses_ или возвращаем нули
    if (!responses_.empty()) {
      auto& resp = responses_.front();
      size_t n = std::min(rx.size(), resp.size());
      std::memcpy(rx.data(), resp.data(), n);
      if (n < rx.size())
        std::memset(rx.data() + n, 0, rx.size() - n);
      responses_.erase(responses_.begin());
    } else {
      std::memset(rx.data(), 0, rx.size());
    }

    return 0;
  }

  // Настройка
  void SetInitResult(int r) { init_result_ = r; }
  void SetTransferResult(int r) { transfer_result_ = r; }
  void PushResponse(std::vector<uint8_t> resp) {
    responses_.push_back(std::move(resp));
  }

  // Инспекция
  int GetTransferCount() const { return transfer_count_; }
  const std::vector<uint8_t>& GetLastTx() const { return last_tx_; }
  void ResetCounters() { transfer_count_ = 0; }

 private:
  int init_result_{0};
  int transfer_result_{0};
  int transfer_count_{0};
  std::vector<uint8_t> last_tx_;
  std::vector<std::vector<uint8_t>> responses_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Вспомогательные функции
// ─────────────────────────────────────────────────────────────────────────────

// Подготовить ответы на успешный Init():
//   - SW reset WriteReg (1 transfer)
//   - ReadReg Product ID (1 transfer, возвращает 0x30)
//   - DoSet WriteReg (1 transfer)
//   - WriteReg CTRL1 (1 transfer)
//   - WriteReg CTRL2 (1 transfer)
static void SetupSuccessfulInit(FakeSpiDevice& spi) {
  // SW reset: нет полезного ответа
  spi.PushResponse({0x00, 0x00});
  // ReadReg Product ID: rx[1] = 0x30
  spi.PushResponse({0x00, 0x30});
  // DoSet WriteReg
  spi.PushResponse({0x00, 0x00});
  // WriteReg CTRL1
  spi.PushResponse({0x00, 0x00});
  // WriteReg CTRL2
  spi.PushResponse({0x00, 0x00});
}

// Сформировать 8-байтовый ответ бёрст-чтения по заданным 18-битным raw-значениям
static std::vector<uint8_t> MakeBurstResponse(uint32_t raw_x, uint32_t raw_y,
                                               uint32_t raw_z) {
  // Разбиваем каждое 18-битное значение на H (биты 17..10), L (9..2) и OL (1..0)
  auto h = [](uint32_t v) -> uint8_t { return static_cast<uint8_t>(v >> 10); };
  auto l = [](uint32_t v) -> uint8_t {
    return static_cast<uint8_t>((v >> 2) & 0xFF);
  };
  uint8_t ol = static_cast<uint8_t>(((raw_x & 0x03) << 6) |
                                     ((raw_y & 0x03) << 4) |
                                     ((raw_z & 0x03) << 2));

  // rx[0] = dummy (адрес), rx[1..7] = данные
  return {0x00, h(raw_x), l(raw_x), h(raw_y), l(raw_y), h(raw_z), l(raw_z), ol};
}

// ─────────────────────────────────────────────────────────────────────────────
// Тесты Init
// ─────────────────────────────────────────────────────────────────────────────

TEST(Mmc5983Test, InitFailsIfSpiInitFails) {
  FakeSpiDevice spi;
  spi.SetInitResult(-1);
  Mmc5983Spi drv(&spi);
  EXPECT_EQ(drv.Init(), -1);
}

TEST(Mmc5983Test, InitFailsIfProductIdWrong) {
  FakeSpiDevice spi;
  // SW reset
  spi.PushResponse({0x00, 0x00});
  // Product ID = 0xFF (неправильный)
  for (int i = 0; i < 5; ++i)
    spi.PushResponse({0x00, 0xFF});

  Mmc5983Spi drv(&spi);
  EXPECT_EQ(drv.Init(), -1);
  EXPECT_NE(drv.GetLastProductId(), 0x30);
}

TEST(Mmc5983Test, InitSucceedsWithCorrectProductId) {
  FakeSpiDevice spi;
  SetupSuccessfulInit(spi);

  Mmc5983Spi drv(&spi);
  EXPECT_EQ(drv.Init(), 0);
  EXPECT_EQ(drv.GetLastProductId(), 0x30);
}

TEST(Mmc5983Test, InitIdempotent) {
  FakeSpiDevice spi;
  SetupSuccessfulInit(spi);

  Mmc5983Spi drv(&spi);
  ASSERT_EQ(drv.Init(), 0);

  int count_before = spi.GetTransferCount();
  // Повторный Init() не должен делать новых Transfer
  EXPECT_EQ(drv.Init(), 0);
  EXPECT_EQ(spi.GetTransferCount(), count_before);
}

// ─────────────────────────────────────────────────────────────────────────────
// Тесты Read
// ─────────────────────────────────────────────────────────────────────────────

TEST(Mmc5983Test, ReadFailsIfNotInitialized) {
  FakeSpiDevice spi;
  Mmc5983Spi drv(&spi);
  MagData data;
  EXPECT_EQ(drv.Read(data), -1);
}

TEST(Mmc5983Test, ReadFailsOnSpiError) {
  FakeSpiDevice spi;
  SetupSuccessfulInit(spi);
  Mmc5983Spi drv(&spi);
  ASSERT_EQ(drv.Init(), 0);

  spi.SetTransferResult(-1);
  MagData data;
  EXPECT_EQ(drv.Read(data), -1);
}

TEST(Mmc5983Test, ReadZeroFieldAtHalfRange) {
  FakeSpiDevice spi;
  SetupSuccessfulInit(spi);
  Mmc5983Spi drv(&spi);
  ASSERT_EQ(drv.Init(), 0);

  // raw = 131072 (2^17) → поле = 0 мГс
  constexpr uint32_t kHalf = 131072;
  spi.PushResponse(MakeBurstResponse(kHalf, kHalf, kHalf));

  MagData data;
  ASSERT_EQ(drv.Read(data), 0);
  EXPECT_NEAR(data.mx, 0.f, 0.01f);
  EXPECT_NEAR(data.my, 0.f, 0.01f);
  EXPECT_NEAR(data.mz, 0.f, 0.01f);
}

TEST(Mmc5983Test, ReadMaxPositiveField) {
  FakeSpiDevice spi;
  SetupSuccessfulInit(spi);
  Mmc5983Spi drv(&spi);
  ASSERT_EQ(drv.Init(), 0);

  // raw = 262143 (2^18 - 1) → (262143 - 131072) * scale = 131071 * (800/131072)
  constexpr uint32_t kMax = 262143;
  constexpr float kExpected = (kMax - 131072.f) * (800.f / 131072.f);
  spi.PushResponse(MakeBurstResponse(kMax, kMax, kMax));

  MagData data;
  ASSERT_EQ(drv.Read(data), 0);
  EXPECT_NEAR(data.mx, kExpected, 0.01f);
  EXPECT_NEAR(data.my, kExpected, 0.01f);
  EXPECT_NEAR(data.mz, kExpected, 0.01f);
}

TEST(Mmc5983Test, ReadMaxNegativeField) {
  FakeSpiDevice spi;
  SetupSuccessfulInit(spi);
  Mmc5983Spi drv(&spi);
  ASSERT_EQ(drv.Init(), 0);

  // raw = 0 → (0 - 131072) * scale = -800 мГс
  spi.PushResponse(MakeBurstResponse(0, 0, 0));

  MagData data;
  ASSERT_EQ(drv.Read(data), 0);
  EXPECT_NEAR(data.mx, -800.f, 0.01f);
  EXPECT_NEAR(data.my, -800.f, 0.01f);
  EXPECT_NEAR(data.mz, -800.f, 0.01f);
}

TEST(Mmc5983Test, ReadIndependentAxes) {
  FakeSpiDevice spi;
  SetupSuccessfulInit(spi);
  Mmc5983Spi drv(&spi);
  ASSERT_EQ(drv.Init(), 0);

  // X = 0 (−800 мГс), Y = 131072 (0 мГс), Z = 262143 (+~800 мГс)
  spi.PushResponse(MakeBurstResponse(0, 131072, 262143));

  MagData data;
  ASSERT_EQ(drv.Read(data), 0);
  EXPECT_NEAR(data.mx, -800.f, 0.01f);
  EXPECT_NEAR(data.my, 0.f, 0.01f);
  EXPECT_GT(data.mz, 799.f);
}

TEST(Mmc5983Test, ReadBurstUsesCorrectStartRegister) {
  FakeSpiDevice spi;
  SetupSuccessfulInit(spi);
  Mmc5983Spi drv(&spi);
  ASSERT_EQ(drv.Init(), 0);

  spi.PushResponse(MakeBurstResponse(131072, 131072, 131072));

  MagData data;
  ASSERT_EQ(drv.Read(data), 0);

  // Первый байт TX должен быть 0x00 | 0x80 = 0x80 (read bit + reg 0)
  const auto& tx = spi.GetLastTx();
  ASSERT_FALSE(tx.empty());
  EXPECT_EQ(tx[0], 0x80);
}

// ─────────────────────────────────────────────────────────────────────────────
// Тест периодического SET/RESET
// ─────────────────────────────────────────────────────────────────────────────

TEST(Mmc5983Test, PeriodicSetResetDoesNotBreakReads) {
  FakeSpiDevice spi;
  SetupSuccessfulInit(spi);
  Mmc5983Spi drv(&spi);
  ASSERT_EQ(drv.Init(), 0);

  constexpr uint32_t kHalf = 131072;

  // Делаем 105 чтений: на 101-м вызове (i=100, read_count_=100) должен быть
  // вставлен доп. WriteReg (SET), драйвер должен продолжать корректно работать.
  for (int i = 0; i < 105; ++i) {
    if (i == 100) {
      // WriteReg SET (2 bytes) — вставляется перед бёрст-чтением
      spi.PushResponse({0x00, 0x00});
    }
    spi.PushResponse(MakeBurstResponse(kHalf, kHalf, kHalf));

    MagData data;
    ASSERT_EQ(drv.Read(data), 0) << "Read failed at iteration " << i;
    EXPECT_NEAR(data.mx, 0.f, 0.01f) << "at iteration " << i;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Тест масштабирования
// ─────────────────────────────────────────────────────────────────────────────

TEST(Mmc5983Test, ScaleMatchesDatasheet) {
  // 1 LSB = 800 мГс / 131072 ≈ 0.006104 мГс
  FakeSpiDevice spi;
  SetupSuccessfulInit(spi);
  Mmc5983Spi drv(&spi);
  ASSERT_EQ(drv.Init(), 0);

  // +1 LSB от нуля поля
  constexpr uint32_t kHalfPlus1 = 131073;
  constexpr float kExpected1Lsb = 800.f / 131072.f;  // ~0.006104 мГс
  spi.PushResponse(MakeBurstResponse(kHalfPlus1, kHalfPlus1, kHalfPlus1));

  MagData data;
  ASSERT_EQ(drv.Read(data), 0);
  EXPECT_NEAR(data.mx, kExpected1Lsb, 1e-4f);
  EXPECT_NEAR(data.my, kExpected1Lsb, 1e-4f);
  EXPECT_NEAR(data.mz, kExpected1Lsb, 1e-4f);
}
