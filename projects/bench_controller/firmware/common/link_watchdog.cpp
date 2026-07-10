#include "link_watchdog.hpp"

namespace bench {

LinkState LinkWatchdog::Update(uint32_t now_ms, bool link_alive) noexcept {
  switch (state_) {
    case LinkState::kRunning:
      if (!link_alive) {
        state_ = LinkState::kGracePeriod;
        link_lost_at_ms_ = now_ms;
      }
      break;

    case LinkState::kGracePeriod:
      if (link_alive) {
        // Единственная точка восстановления без вмешательства оператора.
        state_ = LinkState::kRunning;
      } else if (now_ms - link_lost_at_ms_ >= config_.grace_ms) {
        state_ = LinkState::kRampDown;
        ramp_.Start(now_ms, config_.ramp_ms);
      }
      break;

    case LinkState::kRampDown:
      // Восстановление связи разгрузку не отменяет — доводим до конца.
      if (ramp_.Done(now_ms)) {
        state_ = LinkState::kSafeHold;
      }
      break;

    case LinkState::kSafeHold:
      // Выход только через Reset() (команда оператора).
      break;
  }
  return state_;
}

}  // namespace bench
