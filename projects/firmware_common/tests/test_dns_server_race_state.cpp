#include <gtest/gtest.h>

#include "firmware_common/dns_server_race_state.hpp"

namespace firmware_common {
namespace {

TEST(DnsServerRaceStateTest, NormalStartPublishesSocket) {
  DnsServerRaceState state;

  auto outcome = state.OnBindSucceeded(/*sock=*/7);

  EXPECT_EQ(outcome, DnsServerRaceState::BindOutcome::kPublished);
  EXPECT_TRUE(state.IsSocketPublished());
}

TEST(DnsServerRaceStateTest, StopWhenIdleMarksPendingWithoutSocket) {
  DnsServerRaceState state;
  int sock_to_close = -1;

  auto outcome = state.RequestStop(&sock_to_close);

  EXPECT_EQ(outcome, DnsServerRaceState::StopOutcome::kMarkPending);
  EXPECT_EQ(sock_to_close, -1);
}

// LOS-184 / Codex review bug #1: DnsServerStop() приходит раньше bind() —
// задача должна закрыть сокет сама и не начинать слушать.
TEST(DnsServerRaceStateTest, StopBeforeBindMakesTaskExitImmediately) {
  DnsServerRaceState state;

  int sock_to_close = -1;
  auto stop_outcome = state.RequestStop(&sock_to_close);
  EXPECT_EQ(stop_outcome, DnsServerRaceState::StopOutcome::kMarkPending);
  EXPECT_EQ(sock_to_close, -1);  // Stop() нечего было закрывать

  auto bind_outcome = state.OnBindSucceeded(/*sock=*/7);

  EXPECT_EQ(bind_outcome, DnsServerRaceState::BindOutcome::kStopRequested);
  EXPECT_FALSE(state.IsSocketPublished());
}

// LOS-184 / Codex review bug #2: Stop() после публикации сокета обязан его
// забрать на закрытие — иначе задача продолжит слушать порт 53.
TEST(DnsServerRaceStateTest, StopAfterPublishReturnsSocketToClose) {
  DnsServerRaceState state;
  state.OnBindSucceeded(/*sock=*/7);

  int sock_to_close = -1;
  auto outcome = state.RequestStop(&sock_to_close);

  EXPECT_EQ(outcome, DnsServerRaceState::StopOutcome::kCloseSocket);
  EXPECT_EQ(sock_to_close, 7);
  EXPECT_FALSE(state.IsSocketPublished());
}

// Защита от гонки, обнаруженной при самой доработке (не из review): Stop()
// мог взвести stop_requested_ ровно в момент, когда предыдущая задача
// проваливала bind() по независимой причине и никогда его не consume-ила.
// ResetForNewTask() (вызывается в начале DnsServerStart()) обязан снять этот
// стук, иначе он убьёт следующую легитимную задачу.
TEST(DnsServerRaceStateTest, ResetForNewTaskClearsStalePendingStop) {
  DnsServerRaceState state;
  int sock_to_close = -1;
  state.RequestStop(&sock_to_close);  // Stop() застал сокет неопубликованным

  state.ResetForNewTask();  // следующий DnsServerStart()

  auto bind_outcome = state.OnBindSucceeded(/*sock=*/9);
  EXPECT_EQ(bind_outcome, DnsServerRaceState::BindOutcome::kPublished);
  EXPECT_TRUE(state.IsSocketPublished());
}

TEST(DnsServerRaceStateTest, ResetForNewTaskClearsPublishedSocket) {
  DnsServerRaceState state;
  state.OnBindSucceeded(/*sock=*/7);

  state.ResetForNewTask();

  EXPECT_FALSE(state.IsSocketPublished());
}

}  // namespace
}  // namespace firmware_common
