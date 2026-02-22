#include "failsafe.hpp"

namespace rc_vehicle {

FailsafeState Failsafe::Update(uint32_t now_ms, bool rc_active,
                               bool wifi_active) noexcept {
  // Проверка наличия активных источников
  bool has_active = rc_active || wifi_active;

  // Инициализация при первом вызове
  if (last_update_ms_ == 0 && last_active_ms_ == 0) {
    last_update_ms_ = now_ms;
    // Устанавливаем last_active_ms_ только если есть активное управление
    if (has_active) {
      last_active_ms_ = now_ms;
    }
    // Если нет активного управления при первом вызове, last_active_ms_ остается
    // 0 и failsafe может активироваться сразу при достижении таймаута
  }

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
    uint32_t time_since_active =
        (last_active_ms_ == 0) ? now_ms : (now_ms - last_active_ms_);

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
