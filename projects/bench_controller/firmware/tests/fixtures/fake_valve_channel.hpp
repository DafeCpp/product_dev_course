#pragma once

#include <cstddef>
#include <vector>

#include "valve_channel.hpp"

namespace bench::testing {

/**
 * @brief Управляемый фейк канала клапана для юнит-тестов
 *
 * Обратная связь задаётся тестом, команды накапливаются.
 */
class FakeValveChannel final : public IValveChannel {
 public:
  void WriteSetpoint(const ValveSetpoint& sp) override {
    setpoints.push_back(sp);
  }

  [[nodiscard]] ValveFeedback ReadFeedback() override { return feedback; }

  [[nodiscard]] bool IsOperational() const override { return operational; }

  [[nodiscard]] const ValveSetpoint& Last() const { return setpoints.back(); }

  ValveFeedback feedback{};
  bool operational{true};
  std::vector<ValveSetpoint> setpoints;
};

}  // namespace bench::testing
