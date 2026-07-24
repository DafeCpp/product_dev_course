#include <gtest/gtest.h>

#include "firmware_common/ws_client_failure_tracker.hpp"

namespace firmware_common {
namespace {

using Tracker = WsClientFailureTracker<4>;

TEST(WsClientFailureTrackerTest, AllocatesNewSlotForUnseenFd) {
  Tracker t;

  EXPECT_NE(t.FindOrAllocate(10), Tracker::kNoSlot);
}

TEST(WsClientFailureTrackerTest, ReturnsSameSlotForSameFd) {
  Tracker t;

  int slot1 = t.FindOrAllocate(10);
  int slot2 = t.FindOrAllocate(10);

  EXPECT_EQ(slot1, slot2);
}

TEST(WsClientFailureTrackerTest, DifferentFdsGetDifferentSlots) {
  Tracker t;

  int slot_a = t.FindOrAllocate(10);
  int slot_b = t.FindOrAllocate(20);

  EXPECT_NE(slot_a, slot_b);
}

TEST(WsClientFailureTrackerTest, TableFullReturnsNoSlotForNewFd) {
  Tracker t;  // kMaxClients == 4
  t.FindOrAllocate(1);
  t.FindOrAllocate(2);
  t.FindOrAllocate(3);
  t.FindOrAllocate(4);

  EXPECT_EQ(t.FindOrAllocate(5), Tracker::kNoSlot);
}

TEST(WsClientFailureTrackerTest, RecordFailureIncrementsCount) {
  Tracker t;
  int slot = t.FindOrAllocate(10);

  EXPECT_EQ(t.RecordFailure(slot), 1);
  EXPECT_EQ(t.RecordFailure(slot), 2);
  EXPECT_EQ(t.RecordFailure(slot), 3);
}

TEST(WsClientFailureTrackerTest, RecordSuccessResetsCount) {
  Tracker t;
  int slot = t.FindOrAllocate(10);
  t.RecordFailure(slot);
  t.RecordFailure(slot);

  t.RecordSuccess(slot);

  EXPECT_EQ(t.RecordFailure(slot), 1);
}

TEST(WsClientFailureTrackerTest,
     RecordFailureOnNoSlotIsNoopAndReportsMinusOne) {
  Tracker t;

  EXPECT_EQ(t.RecordFailure(Tracker::kNoSlot), -1);
}

TEST(WsClientFailureTrackerTest, EvictFreesSlotForReuseWithFreshCounter) {
  Tracker t;
  int slot = t.FindOrAllocate(10);
  t.RecordFailure(slot);
  t.RecordFailure(slot);

  t.Evict(slot);

  int reused = t.FindOrAllocate(99);
  EXPECT_EQ(reused, slot);
  EXPECT_EQ(t.RecordFailure(reused), 1);  // счётчик нового fd начался с нуля
}

TEST(WsClientFailureTrackerTest, GarbageCollectClearsDisconnectedFdsOnly) {
  Tracker t;
  int slot_a = t.FindOrAllocate(10);
  int slot_b = t.FindOrAllocate(20);
  t.RecordFailure(slot_a);
  t.RecordFailure(slot_b);

  const int still_connected[] = {20};
  t.GarbageCollect(still_connected, 1);

  // fd 10 ушёл из списка клиентов — слот освобождён и счётчик сброшен.
  int reused = t.FindOrAllocate(10);
  EXPECT_EQ(t.RecordFailure(reused), 1);
  // fd 20 остался подключён — счётчик не тронут.
  EXPECT_EQ(t.RecordFailure(slot_b), 2);
}

TEST(WsClientFailureTrackerTest, GarbageCollectWithEmptyListClearsAll) {
  Tracker t;
  t.FindOrAllocate(10);
  t.FindOrAllocate(20);

  t.GarbageCollect(nullptr, 0);

  // Оба слота свободны — 4 новых fd должны получить слоты без коллизий.
  EXPECT_NE(t.FindOrAllocate(1), Tracker::kNoSlot);
  EXPECT_NE(t.FindOrAllocate(2), Tracker::kNoSlot);
  EXPECT_NE(t.FindOrAllocate(3), Tracker::kNoSlot);
  EXPECT_NE(t.FindOrAllocate(4), Tracker::kNoSlot);
}

}  // namespace
}  // namespace firmware_common
