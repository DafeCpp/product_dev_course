// CANopen-адаптер против FakeCanBus: проверяем поведение мастера на
// уровне сырых кадров — bootup, SYNC-продюсер, кодирование setpoint-TPDO,
// применение feedback-RPDO в OD, heartbeat-consumer.
//
// COB-карта (canopen/README.md): мастер 0x01, клапан 0x20;
// TPDO1 мастера 0x220, RPDO1 мастера 0x1A0, SYNC 0x080, HB 0x701/0x720.

#include <gtest/gtest.h>

#include "bench_types.hpp"
#include "canopen_valve_channel.hpp"
#include "co_master.hpp"
#include "fixtures/fake_can_bus.hpp"

namespace bench {
namespace {

constexpr uint32_t kTickUs = 2000;  // 500 Гц

class CanopenAdapterTest : public ::testing::Test {
 protected:
  void SetUp() override {
    channel.Attach(master);  // extension до Init — см. комментарий Attach
    ASSERT_TRUE(master.Init(bus, {.node_id = 0x01, .valve_node_id = 0x20}));
    // NMT мастера переходит startup → operational в первом CO_process;
    // до этого PDO-тракт закрыт. Прокручиваем транспорт.
    TransportTick();
    TransportTick();
  }

  // Полный тик транспорта без шага регулятора
  void TransportTick() {
    channel.PreTick(kTickUs);
    (void)channel.ReadFeedback();
    channel.PostTick(kTickUs);
  }

  bench::testing::FakeCanBus bus;
  CoMaster master;
  CanopenValveChannel channel;
};

TEST_F(CanopenAdapterTest, SendsBootupAndNmtStartToValve) {
  // Bootup мастера — кадр 0x700+1 с payload 0x00 (HB тоже 0x701,
  // но с NMT-состоянием ≠ 0).
  bool bootup_seen = false;
  for (const auto& f : bus.SentWithId(0x701)) {
    if (f.data[0] == 0x00) bootup_seen = true;
  }
  EXPECT_TRUE(bootup_seen);

  const auto nmt = bus.SentWithId(0x000);
  ASSERT_GE(nmt.size(), 1u);
  EXPECT_EQ(nmt[0].data[0], 0x01);  // Start remote node
  EXPECT_EQ(nmt[0].data[1], 0x20);  // узел клапана
}

TEST_F(CanopenAdapterTest, ProducesSyncEveryTick) {
  bus.sent.clear();
  for (int i = 0; i < 10; ++i) TransportTick();
  // 0x1006 = 2000 мкс = период тика ⇒ SYNC в каждом тике (±1 на фазу).
  const auto syncs = bus.SentWithId(0x080);
  EXPECT_GE(syncs.size(), 9u);
  EXPECT_LE(syncs.size(), 11u);
}

TEST_F(CanopenAdapterTest, SetpointTpdoEncodesScaledPayload) {
  bus.sent.clear();
  channel.WriteSetpoint(
      {.mode = ControlMode::kForce, .value = 0.5f, .enable = true});
  channel.PostTick(kTickUs);

  const auto tpdo = bus.SentWithId(0x220);
  ASSERT_EQ(tpdo.size(), 1u);
  EXPECT_EQ(tpdo[0].dlc, 3u);
  const auto raw =
      static_cast<int16_t>(static_cast<uint16_t>(tpdo[0].data[0]) |
                           (static_cast<uint16_t>(tpdo[0].data[1]) << 8));
  EXPECT_EQ(raw, PdoScaling::CommandToRaw(0.5f));
  EXPECT_EQ(tpdo[0].data[2], 0x80);  // enable | force(0)
}

TEST_F(CanopenAdapterTest, NoTpdoWithoutSetpointWrite) {
  bus.sent.clear();
  channel.PostTick(kTickUs);  // без WriteSetpoint
  EXPECT_TRUE(bus.SentWithId(0x220).empty());
}

TEST_F(CanopenAdapterTest, FeedbackRpdoLandsInReadFeedback) {
  const int16_t force_raw = PdoScaling::ForceToRaw(25'000.0f);
  const int16_t pos_raw = PdoScaling::PositionToRaw(5.0f);
  bus.Inject(0x1A0, {static_cast<uint8_t>(force_raw & 0xFF),
                     static_cast<uint8_t>((force_raw >> 8) & 0xFF),
                     static_cast<uint8_t>(pos_raw & 0xFF),
                     static_cast<uint8_t>((pos_raw >> 8) & 0xFF), 0x07});

  channel.PreTick(kTickUs);
  const ValveFeedback fb = channel.ReadFeedback();
  channel.PostTick(kTickUs);

  EXPECT_TRUE(fb.fresh);
  EXPECT_EQ(fb.age_ticks, 0u);
  EXPECT_NEAR(fb.force_n, 25'000.0f, 5.0f);  // квант ~3 Н
  EXPECT_NEAR(fb.position_mm, 5.0f, 0.01f);  // квант ~3 мкм
  EXPECT_EQ(fb.status, 0x07);

  // Следующий тик без нового кадра — данные несвежие, возраст растёт.
  channel.PreTick(kTickUs);
  const ValveFeedback stale = channel.ReadFeedback();
  EXPECT_FALSE(stale.fresh);
  EXPECT_EQ(stale.age_ticks, 1u);
  EXPECT_NEAR(stale.force_n, 25'000.0f, 5.0f);  // значение сохранилось
}

TEST_F(CanopenAdapterTest, HeartbeatDrivesIsOperational) {
  // До первого HB клапана узел не operational.
  EXPECT_FALSE(channel.IsOperational());

  // HB клапана: state operational (0x05).
  bus.Inject(0x720, {0x05});
  TransportTick();
  EXPECT_TRUE(channel.IsOperational());

  // Без HB дольше таймаута (200 мс = 100 тиков) — узел потерян.
  for (int i = 0; i < 120; ++i) TransportTick();
  EXPECT_FALSE(channel.IsOperational());

  // HB вернулся — восстановление.
  bus.Inject(0x720, {0x05});
  TransportTick();
  EXPECT_TRUE(channel.IsOperational());
}

TEST_F(CanopenAdapterTest, MasterHeartbeatProducedPeriodically) {
  bus.sent.clear();
  // 0x1017 = 100 мс: за 500 тиков (1 с) — ~10 HB мастера.
  for (int i = 0; i < 500; ++i) TransportTick();
  const auto hb = bus.SentWithId(0x701);
  EXPECT_GE(hb.size(), 9u);
  EXPECT_LE(hb.size(), 11u);
}

}  // namespace
}  // namespace bench
