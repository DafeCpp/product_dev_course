#include "failsafe.hpp"

#include <mutex>

namespace rc_vehicle {

FailsafeState Failsafe::Update(uint32_t now_ms, bool rc_active,
                               bool wifi_active) noexcept {
  std::lock_guard<std::mutex> lock(mutex_);

  // Проверка наличия активных источников
  bool has_active = rc_active || wifi_active;

  // Инициализация при первом вызове
  if (!initialized_) {
    initialized_ = true;
    last_active_ms_ = now_ms;
    last_update_ms_ = now_ms;
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
    // Защита от переполнения: если last_active_ms_ > now_ms, считаем таймаут
    // истёкшим
    uint32_t time_since_active =
        (last_active_ms_ > now_ms) ? timeout_ms_ : (now_ms - last_active_ms_);

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

bool Failsafe::IsActive() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_ == FailsafeState::Active;
}

FailsafeState Failsafe::GetState() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

uint32_t Failsafe::GetTimeSinceLastActive(uint32_t now_ms) const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  if (last_active_ms_ == 0) return 0;
  // Защита от переполнения: если last_active_ms_ > now_ms — таймер
  // переполнился, считаем что прошло очень много времени (как в Update()).
  if (last_active_ms_ > now_ms) return UINT32_MAX;
  return now_ms - last_active_ms_;
}

void Failsafe::SetTimeout(uint32_t timeout_ms) noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  timeout_ms_ = timeout_ms;
}

uint32_t Failsafe::GetTimeout() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return timeout_ms_;
}

void Failsafe::Reset() noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  state_ = FailsafeState::Inactive;
  last_active_ms_ = 0;
  last_update_ms_ = 0;
  initialized_ = false;
}

}  // namespace rc_vehicle
