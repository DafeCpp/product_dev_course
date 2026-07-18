#pragma once

#include <string>

#include "i_can_bus.hpp"

namespace bench {

/**
 * @brief ICanBus поверх Linux SocketCAN (vcan0 / can0)
 *
 * Неблокирующий RAW-сокет; используется измерительным ригом
 * (sim_host --socketcan) и недоступен в юнит-тестах/CI (там FakeCanBus).
 */
class SocketCanBus final : public ICanBus {
 public:
  ~SocketCanBus() override;

  /// Открыть и привязать сокет к интерфейсу (например "vcan0")
  [[nodiscard]] bool Open(const std::string& ifname);

  [[nodiscard]] bool Send(const CanFrame& frame) override;
  [[nodiscard]] std::optional<CanFrame> Poll() override;

  [[nodiscard]] bool IsOpen() const { return fd_ >= 0; }

 private:
  int fd_{-1};
};

}  // namespace bench
