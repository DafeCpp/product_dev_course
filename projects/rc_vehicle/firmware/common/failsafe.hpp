#pragma once

#include <cstdint>
#include <mutex>

namespace rc_vehicle {

/**
 * @brief Состояние failsafe
 */
enum class FailsafeState : uint8_t {
  Inactive = 0,  ///< Failsafe неактивен (есть управление)
  Active,        ///< Failsafe активен (нет управления)
  Recovering     ///< Восстановление после failsafe
};

/**
 * @brief Failsafe - защита от потери управления
 *
 * Отслеживает наличие активных источников управления (RC, Wi-Fi) и активирует
 * failsafe при их отсутствии в течение заданного таймаута.
 *
 * @example
 * @code
 * Failsafe fs(250);  // 250 ms timeout
 *
 * uint32_t now = GetTimeMs();
 * bool rc_active = CheckRc();
 * bool wifi_active = CheckWifi();
 *
 * auto state = fs.Update(now, rc_active, wifi_active);
 * if (state == FailsafeState::Active) {
 *   SetPwmNeutral();  // нейтраль
 * }
 * @endcode
 */
class Failsafe {
 public:
  /**
   * @brief Конструктор
   * @param timeout_ms Таймаут в миллисекундах (по умолчанию 250 ms)
   */
  explicit Failsafe(uint32_t timeout_ms = 250) noexcept
      : timeout_ms_(timeout_ms) {}

  /**
   * @brief Обновить состояние failsafe
   *
   * Должен вызываться периодически (например, каждые 10 ms) с текущим временем
   * и статусом источников управления.
   *
   * @param now_ms Текущее время в миллисекундах (монотонное)
   * @param rc_active RC-приёмник активен (получены валидные сигналы)
   * @param wifi_active Wi-Fi команды активны (получены недавно)
   * @return Новое состояние failsafe
   */
  [[nodiscard]] FailsafeState Update(uint32_t now_ms, bool rc_active,
                                     bool wifi_active) noexcept;

  /**
   * @brief Проверить, активен ли failsafe
   * @return true, если failsafe активен (нет управления)
   */
  [[nodiscard]] bool IsActive() const noexcept;

  /**
   * @brief Получить текущее состояние
   * @return Состояние failsafe
   */
  [[nodiscard]] FailsafeState GetState() const noexcept;

  /**
   * @brief Получить время с момента последнего активного источника
   * @param now_ms Текущее время в миллисекундах
   * @return Время в миллисекундах с момента последнего активного источника
   */
  [[nodiscard]] uint32_t GetTimeSinceLastActive(
      uint32_t now_ms) const noexcept;

  /**
   * @brief Установить таймаут
   * @param timeout_ms Новый таймаут в миллисекундах
   */
  void SetTimeout(uint32_t timeout_ms) noexcept;

  /**
   * @brief Получить текущий таймаут
   * @return Таймаут в миллисекундах
   */
  [[nodiscard]] uint32_t GetTimeout() const noexcept;

  /**
   * @brief Сбросить состояние failsafe
   *
   * Переводит failsafe в состояние Inactive и сбрасывает таймеры.
   * Используется при ручном восстановлении или перезапуске системы.
   */
  void Reset() noexcept;

 private:
  mutable std::mutex mutex_;
  FailsafeState state_{FailsafeState::Inactive};
  uint32_t last_active_ms_{0};
  uint32_t last_update_ms_{0};
  uint32_t timeout_ms_;
  bool initialized_{false};
};

}  // namespace rc_vehicle
