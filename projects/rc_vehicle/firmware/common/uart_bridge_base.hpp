#pragma once

#include "protocol.hpp"
#include <concepts>
#include <cstddef>
#include <optional>

/** Результат приёма команды (газ, руль). */
struct UartCommand {
  float throttle{0.f};
  float steering{0.f};
};

/** Буфер для записи в UART: непрерывная последовательность байт (const). */
template <typename T>
concept WritableToUart = requires(const T &t) {
  { t.data() } -> std::convertible_to<const uint8_t *>;
  { t.size() } -> std::convertible_to<size_t>;
};

/** Буфер для чтения из UART: непрерывная последовательность байт (mutable). */
template <typename T>
concept ReadableFromUart = requires(T &t) {
  { t.data() } -> std::convertible_to<uint8_t *>;
  { t.size() } -> std::convertible_to<size_t>;
};

/**
 * Базовый класс UART-моста (MCU ↔ ESP32).
 * Наследники реализуют Init(), Write(), ReadAvailable() под конкретный чип.
 * Логика протокола (кадры TELEM/COMMAND) и буфер приёма — в базе.
 */
class UartBridgeBase {
public:
  static constexpr size_t RX_BUF_SIZE = 1024;

  virtual ~UartBridgeBase() = default;

  virtual int Init() = 0;
  /** Записать в UART. Возврат: 0 при успехе, -1 при ошибке. */
  virtual int Write(const uint8_t *data, size_t len) = 0;
  /** Прочитать доступные байты (неблокирующий). Возврат: число прочитанных, 0
   * если нет данных, -1 при ошибке. */
  virtual int ReadAvailable(uint8_t *buf, size_t max_len) = 0;

  /** Запись из контейнера (std::vector, std::array, std::span и т.п.). */
  template <WritableToUart Container> int Write(const Container &data) {
    return Write(data.data(), data.size());
  }
  /** Чтение в контейнер. size() — макс. байт для чтения. */
  template <ReadableFromUart Container> int ReadAvailable(Container &buf) {
    return ReadAvailable(buf.data(), buf.size());
  }

  // --- API для MCU (RP2040/STM32): отправка телеметрии, приём команд ---
  int SendTelem(const TelemetryData &telem_data);

  /** Принять команду от ESP32. std::nullopt, если кадра нет или он невалидный.
   */
  std::optional<UartCommand> ReceiveCommand();

  // --- API для ESP32: отправка команд, приём телеметрии ---
  int SendCommand(float throttle, float steering);
  int ReceiveTelem(TelemetryData *telem_data);

protected:
  UartBridgeBase() : rx_pos_(0) {}
  uint8_t rx_buffer_[RX_BUF_SIZE];
  size_t rx_pos_;

  void PumpRx();
};
