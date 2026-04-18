#pragma once

#include <algorithm>
#include <cmath>

namespace rc_vehicle {

/**
 * @brief Команда управления (RC или Wi-Fi)
 *
 * Инкапсулирует значения газа и руля с автоматической валидацией
 * в диапазоне [-1..1]. Предоставляет полезные методы для работы с командами.
 *
 * @example
 * @code
 * RcCommand cmd(0.5f, -0.3f);  // throttle=0.5, steering=-0.3
 *
 * if (cmd.IsNeutral()) {
 *   // команда нейтральна
 * }
 *
 * auto scaled = cmd.Scaled(0.5f);  // уменьшить в 2 раза
 * @endcode
 */
class RcCommand {
 public:
  /**
   * @brief Конструктор по умолчанию (нейтральная команда)
   */
  RcCommand() = default;

  /**
   * @brief Конструктор с валидацией
   * @param throttle Газ [-1..1]
   * @param steering Руль [-1..1]
   */
  RcCommand(float throttle, float steering) noexcept
      : throttle_(Clamp(throttle)), steering_(Clamp(steering)) {}

  /**
   * @brief Получить значение газа
   * @return Газ [-1..1]
   */
  [[nodiscard]] float throttle() const noexcept { return throttle_; }

  /**
   * @brief Получить значение руля
   * @return Руль [-1..1]
   */
  [[nodiscard]] float steering() const noexcept { return steering_; }

  /**
   * @brief Установить значение газа с валидацией
   * @param value Новое значение
   */
  void set_throttle(float value) noexcept { throttle_ = Clamp(value); }

  /**
   * @brief Установить значение руля с валидацией
   * @param value Новое значение
   */
  void set_steering(float value) noexcept { steering_ = Clamp(value); }

  /**
   * @brief Проверить, является ли команда нейтральной
   * @param threshold Порог для определения нейтрали (по умолчанию 0.01)
   * @return true, если оба значения близки к нулю
   */
  [[nodiscard]] bool IsNeutral(float threshold = 0.01f) const noexcept {
    return std::abs(throttle_) < threshold && std::abs(steering_) < threshold;
  }

  /**
   * @brief Получить команду с гарантированно валидными значениями
   * @return Копия команды (уже валидна)
   */
  [[nodiscard]] RcCommand Clamped() const noexcept {
    return RcCommand(throttle_, steering_);
  }

  /**
   * @brief Масштабировать команду
   * @param scale Коэффициент масштабирования
   * @return Новая команда с масштабированными значениями
   */
  [[nodiscard]] RcCommand Scaled(float scale) const noexcept {
    return RcCommand(throttle_ * scale, steering_ * scale);
  }

  /**
   * @brief Инвертировать газ
   * @return Новая команда с инвертированным газом
   */
  [[nodiscard]] RcCommand InvertedThrottle() const noexcept {
    return RcCommand(-throttle_, steering_);
  }

  /**
   * @brief Инвертировать руль
   * @return Новая команда с инвертированным рулём
   */
  [[nodiscard]] RcCommand InvertedSteering() const noexcept {
    return RcCommand(throttle_, -steering_);
  }

  /**
   * @brief Получить максимальное абсолютное значение
   * @return max(|throttle|, |steering|)
   */
  [[nodiscard]] float MaxAbsValue() const noexcept {
    return std::max(std::abs(throttle_), std::abs(steering_));
  }

  /**
   * @brief Операторы сравнения
   */
  [[nodiscard]] bool operator==(const RcCommand& other) const noexcept {
    return throttle_ == other.throttle_ && steering_ == other.steering_;
  }

  [[nodiscard]] bool operator!=(const RcCommand& other) const noexcept {
    return !(*this == other);
  }

 private:
  float throttle_{0.0f};  ///< Газ [-1..1]
  float steering_{0.0f};  ///< Руль [-1..1]

  /**
   * @brief Ограничить значение в диапазоне [-1..1]
   * @param value Исходное значение
   * @return Ограниченное значение
   */
  [[nodiscard]] static float Clamp(float value) noexcept {
    return std::clamp(value, -1.0f, 1.0f);
  }
};

}  // namespace rc_vehicle