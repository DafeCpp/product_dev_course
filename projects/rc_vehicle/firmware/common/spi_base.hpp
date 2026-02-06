#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

/**
 * SPI абстракции.
 *
 * - `SpiBus` — абстракция SPI-шины/периферии (только инициализация).
 * - `SpiDevice` — абстракция устройства на SPI-шине: `Transfer()` выполняет
 *   полнодуплексный обмен и должна держать CS активным на время всего обмена
 *   (CS low → обмен → CS high).
 *
 * Платформы реализуют конкретные классы шины/устройства (ESP32, RP2040, STM32).
 */
class SpiBus {
 public:
  virtual ~SpiBus() = default;

  /** Инициализация SPI-шины. Возврат: 0 при успехе, -1 при ошибке. */
  virtual int Init() = 0;
};

class SpiDevice {
 public:
  virtual ~SpiDevice() = default;

  /** Инициализация устройства (в т.ч. bus). Возврат: 0/-1. */
  virtual int Init() = 0;

  /**
   * Полнодуплексный обмен: отправить len байт из tx, принять len байт в rx.
   * Реализация держит CS активным на время всего обмена.
   * @param tx данные для отправки
   * @param rx буфер приёма (может совпадать по памяти с tx для in-place)
   * Требование: tx.size() == rx.size() и size > 0.
   * @return 0 при успехе, -1 при ошибке
   */
  virtual int Transfer(std::span<const uint8_t> tx, std::span<uint8_t> rx) = 0;
};
