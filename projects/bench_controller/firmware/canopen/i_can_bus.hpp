#pragma once

#include <cstdint>
#include <optional>

namespace bench {

/**
 * @brief CAN-кадр (classic CAN, 11-битный идентификатор)
 */
struct CanFrame {
  uint32_t id{0};
  uint8_t dlc{0};
  uint8_t data[8]{};
};

/**
 * @brief Фреймовый интерфейс CAN-шины
 *
 * Реализации: SocketCanBus (Linux/vcan), FakeCanBus (тесты),
 * TwaiCanBus (ESP32, PR-C). Обе стороны неблокирующие: Send может
 * вернуть false (перегрузка), Poll — nullopt (кадров нет).
 */
class ICanBus {
 public:
  virtual ~ICanBus() = default;
  [[nodiscard]] virtual bool Send(const CanFrame& frame) = 0;
  [[nodiscard]] virtual std::optional<CanFrame> Poll() = 0;
};

}  // namespace bench
