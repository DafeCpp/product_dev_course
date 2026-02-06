#pragma once

#include "spi_base.hpp"

/** Реализация SpiBase для STM32 (STM32Cube LL). Пины и периферия — board_pins.hpp. */
class SpiStm32 : public SpiBase {
 public:
  int Init() override;
  int Transfer(std::span<const uint8_t> tx, std::span<uint8_t> rx) override;
};
