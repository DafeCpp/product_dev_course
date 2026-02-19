#include "failsafe.hpp"

namespace rc_vehicle {

FailsafeState Failsafe::Update(uint32_t now_ms, bool rc_active,
                               bool wifi_active) noexcept {
  // Инициализация при первом вызове
  if (last_update_ms_ == 0) {
    last_update_ms_ = now_ms;
    last_active_ms_ = now_ms;
  }

  // Проверка наличия активных источников
  bool has_active = rc_active || wifi_active;

  if (has_active) {
    // Есть активный источник - обновляем время последней активности
    last_active_ms_ = now_ms;

    // Переход из Active в Recovering
    if (state_ == FailsafeState::Active) {
      state_ = FailsafeState::Recovering;
    }
    // Переход из Recovering в Inactive (восстановление завершено)
    else if (state_ == FailsafeState::Recovering) {
      state_ = FailsafeState::Inactive;
    }
  } else {
    // Нет активных источников - проверяем таймаут
    uint32_t time_since_active = now_ms - last_active_ms_;

    if (time_since_active >= timeout_ms_) {
      // Таймаут истёк - активируем failsafe
      state_ = FailsafeState::Active;
    }
    // Если ещё не истёк таймаут, но уже нет источников - остаёмся в текущем
    // состоянии
  }

  last_update_ms_ = now_ms;
  return state_;
}

}  // namespace rc_vehicle
