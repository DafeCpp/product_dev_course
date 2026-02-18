#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <optional>
#include <span>

#include "protocol.hpp"

namespace rc_vehicle {

// ═══════════════════════════════════════════════════════════════════════════
// Типы данных
// ═══════════════════════════════════════════════════════════════════════════

/** Результат приёма команды (газ, руль). */
struct UartCommand {
  float throttle{0.f};
  float steering{0.f};

  [[nodiscard]] UartCommand Clamped() const noexcept {
    auto clamp = [](float val) -> float {
      if (val > 1.0f) return 1.0f;
      if (val < -1.0f) return -1.0f;
      return val;
    };
    return UartCommand{clamp(throttle), clamp(steering)};
  }
};

/** Ошибки UART операций. */
enum class UartError {
  WriteFailure,
  ReadFailure,
  BufferOverflow,
  ProtocolError
};

// ═══════════════════════════════════════════════════════════════════════════
// Буфер приёма с RAII
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Буфер приёма UART с управлением позицией.
 * Инкапсулирует логику работы с кольцевым буфером.
 */
class RxBuffer {
 public:
  static constexpr size_t CAPACITY = 1024;

  RxBuffer() = default;

  /**
   * Получить span для записи новых данных.
   * @return Доступное пространство для записи
   */
  [[nodiscard]] std::span<uint8_t> Available() noexcept {
    return std::span(data_.data() + pos_, CAPACITY - pos_);
  }

  /**
   * Получить span текущих данных.
   * @return Данные в буфере
   */
  [[nodiscard]] std::span<const uint8_t> Data() const noexcept {
    return std::span(data_.data(), pos_);
  }

  /**
   * Продвинуть позицию записи на n байт.
   * @param n Количество записанных байт
   */
  void Advance(size_t n) noexcept {
    pos_ += n;
    if (pos_ > CAPACITY) pos_ = CAPACITY;
  }

  /**
   * Потребить n байт из начала буфера.
   * @param n Количество байт для удаления
   */
  void Consume(size_t n) noexcept;

  /**
   * Выровнять буфер: найти начало кадра и сдвинуть данные.
   * @return true если найдено начало кадра
   */
  bool Align() noexcept;

  /**
   * Пропустить один байт (для обработки ложных префиксов).
   */
  void SkipOne() noexcept;

  /**
   * Сбросить буфер.
   */
  void Reset() noexcept { pos_ = 0; }

  /**
   * Проверить, заполнен ли буфер.
   */
  [[nodiscard]] bool IsFull() const noexcept { return pos_ >= CAPACITY; }

  /**
   * Получить текущую позицию.
   */
  [[nodiscard]] size_t Position() const noexcept { return pos_; }

 private:
  std::array<uint8_t, CAPACITY> data_{};
  size_t pos_{0};
};

// ═══════════════════════════════════════════════════════════════════════════
// Концепты
// ═══════════════════════════════════════════════════════════════════════════

/** Непрерывный буфер байт для чтения/записи UART (data(), size()). */
template <typename T>
concept Bufferable = requires(T &t) {
  { t.data() } -> std::convertible_to<const uint8_t *>;
  { t.size() } -> std::convertible_to<size_t>;
};

// ═══════════════════════════════════════════════════════════════════════════
// Базовый класс UART-моста
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Базовый класс UART-моста (MCU ↔ ESP32).
 * Наследники реализуют Init(), Write(), ReadAvailable() под конкретный чип.
 * Логика протокола (кадры TELEM/COMMAND) и буфер приёма — в базе.
 */
class UartBridgeBase {
 public:
  virtual ~UartBridgeBase() = default;

  /**
   * Инициализация UART.
   * @return 0 при успехе, -1 при ошибке
   */
  virtual int Init() = 0;

  // ─────────────────────────────────────────────────────────────────────────
  // Шаблонные методы для работы с контейнерами
  // ─────────────────────────────────────────────────────────────────────────

  /** Запись из контейнера (std::vector, std::array, std::span и т.п.). */
  template <Bufferable Container>
  int Write(const Container &data) {
    return Write(data.data(), data.size());
  }

  /** Чтение в контейнер. size() — макс. байт для чтения. */
  template <Bufferable Container>
  int ReadAvailable(Container &buf) {
    return ReadAvailable(buf.data(), buf.size());
  }

  // ─────────────────────────────────────────────────────────────────────────
  // API для MCU (RP2040/STM32): отправка телеметрии, приём команд
  // ─────────────────────────────────────────────────────────────────────────

  /**
   * Отправить телеметрию на ESP32 (MCU → ESP32).
   * @param telem_data Данные телеметрии
   * @return 0 при успехе, -1 при ошибке
   */
  int SendTelem(const protocol::TelemetryData &telem_data);

  /**
   * Принять команду от ESP32 (MCU ← ESP32).
   * @return Команда или std::nullopt если кадра нет/невалиден
   */
  [[nodiscard]] std::optional<UartCommand> ReceiveCommand();

  /**
   * Принять PING от ESP32 (MCU должен ответить SendPong).
   * @return true если PING получен
   */
  [[nodiscard]] bool ReceivePing();

  /**
   * Отправить PONG в ответ на PING (MCU → ESP32).
   * @return 0 при успехе, -1 при ошибке
   */
  int SendPong();

  /**
   * Отправить LOG-сообщение на ESP32 (MCU → ESP32).
   * @param msg Текст сообщения
   * @param len Длина сообщения
   * @return 0 при успехе, -1 при ошибке
   */
  int SendLog(const char *msg, size_t len);

  // ─────────────────────────────────────────────────────────────────────────
  // API для ESP32: отправка команд, приём телеметрии
  // ─────────────────────────────────────────────────────────────────────────

  /**
   * Отправить команду на MCU (ESP32 → MCU).
   * @param throttle Газ [-1.0, 1.0]
   * @param steering Руль [-1.0, 1.0]
   * @return 0 при успехе, -1 при ошибке
   */
  int SendCommand(float throttle, float steering);

  /**
   * Принять телеметрию от MCU (ESP32 ← MCU).
   * @return Телеметрия или std::nullopt если кадра нет/невалиден
   */
  [[nodiscard]] std::optional<protocol::TelemetryData> ReceiveTelem();

  /**
   * Принять LOG от MCU (ESP32 ← MCU).
   * @param buf Буфер для сообщения
   * @param max_len Максимальная длина
   * @return Длина сообщения (>0) или 0 если нет
   */
  int ReceiveLog(char *buf, size_t max_len);

  /**
   * Отправить PING на MCU (ESP32 → MCU).
   * @return 0 при успехе, -1 при ошибке
   */
  int SendPing();

  /**
   * Принять PONG от MCU (ESP32 ← MCU).
   * @return true если PONG получен
   */
  [[nodiscard]] bool ReceivePong();

 protected:
  /**
   * Записать в UART (платформенная реализация).
   * @param data Данные для записи
   * @param len Длина данных
   * @return 0 при успехе, -1 при ошибке
   */
  virtual int Write(const uint8_t *data, size_t len) = 0;

  /**
   * Прочитать доступные байты (неблокирующий, платформенная реализация).
   * @param buf Буфер для чтения
   * @param max_len Максимальное количество байт
   * @return Число прочитанных байт, 0 если нет данных, -1 при ошибке
   */
  virtual int ReadAvailable(uint8_t *buf, size_t max_len) = 0;

  UartBridgeBase() = default;

 private:
  RxBuffer rx_buffer_;

  /**
   * Прочитать данные из UART в буфер приёма.
   */
  void PumpRx();

  /**
   * Шаблонный метод для приёма кадров (устраняет дублирование).
   * @tparam T Тип данных для парсинга
   * @param expected_type Ожидаемый тип сообщения
   * @param parse_func Функция парсинга
   * @return Распарсенные данные или std::nullopt
   */
  template <typename T>
  [[nodiscard]] std::optional<T> ReceiveFrame(
      protocol::MessageType expected_type,
      protocol::Result<T> (*parse_func)(std::span<const uint8_t>));
};

}  // namespace rc_vehicle
