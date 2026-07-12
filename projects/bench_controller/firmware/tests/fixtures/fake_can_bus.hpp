#pragma once

#include <deque>
#include <vector>

#include "i_can_bus.hpp"

namespace bench::testing {

/**
 * @brief In-memory ICanBus для юнит-тестов CANopen-слоя
 *
 * Отправленные стеком кадры складываются в sent (тест их разбирает),
 * входящие тест подкладывает через Inject.
 */
class FakeCanBus final : public ICanBus {
 public:
  [[nodiscard]] bool Send(const CanFrame& frame) override {
    sent.push_back(frame);
    return !fail_send;
  }

  [[nodiscard]] std::optional<CanFrame> Poll() override {
    if (rx_queue.empty()) return std::nullopt;
    CanFrame f = rx_queue.front();
    rx_queue.pop_front();
    return f;
  }

  void Inject(uint32_t id, std::initializer_list<uint8_t> bytes) {
    CanFrame f{};
    f.id = id;
    f.dlc = static_cast<uint8_t>(bytes.size());
    uint8_t i = 0;
    for (uint8_t b : bytes) f.data[i++] = b;
    rx_queue.push_back(f);
  }

  /// Кадры с данным COB-ID среди отправленных
  [[nodiscard]] std::vector<CanFrame> SentWithId(uint32_t id) const {
    std::vector<CanFrame> out;
    for (const auto& f : sent) {
      if (f.id == id) out.push_back(f);
    }
    return out;
  }

  std::vector<CanFrame> sent;
  std::deque<CanFrame> rx_queue;
  bool fail_send{false};
};

}  // namespace bench::testing
