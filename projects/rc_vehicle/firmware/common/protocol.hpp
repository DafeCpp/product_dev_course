#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

namespace rc_vehicle::protocol {

// ═══════════════════════════════════════════════════════════════════════════
// Константы протокола
// ═══════════════════════════════════════════════════════════════════════════

inline constexpr uint8_t FRAME_PREFIX_0 = 0xAA;
inline constexpr uint8_t FRAME_PREFIX_1 = 0x55;
inline constexpr uint8_t PROTOCOL_VERSION = 0x01;

inline constexpr size_t HEADER_SIZE =
    6;  // prefix(2) + ver(1) + type(1) + len(2)
inline constexpr size_t CRC_SIZE = 2;
inline constexpr size_t MIN_FRAME_SIZE = HEADER_SIZE + CRC_SIZE;
inline constexpr size_t LOG_MAX_PAYLOAD = 200;

// ═══════════════════════════════════════════════════════════════════════════
// Типы сообщений
// ═══════════════════════════════════════════════════════════════════════════

enum class MessageType : uint8_t {
  Command = 0x01,
  Telemetry = 0x02,
  Ping = 0x03,
  Pong = 0x04,
  Log = 0x05
};

// ═══════════════════════════════════════════════════════════════════════════
// Ошибки парсинга
// ═══════════════════════════════════════════════════════════════════════════

enum class ParseError {
  InsufficientData,
  InvalidPrefix,
  InvalidVersion,
  InvalidType,
  InvalidPayloadLength,
  CrcMismatch,
  BufferTooSmall
};

// ═══════════════════════════════════════════════════════════════════════════
// Result type (альтернатива std::expected для C++23)
// ═══════════════════════════════════════════════════════════════════════════

template <typename T>
using Result = std::variant<T, ParseError>;

template <typename T>
[[nodiscard]] inline bool IsOk(const Result<T>& r) noexcept {
  return std::holds_alternative<T>(r);
}

template <typename T>
[[nodiscard]] inline bool IsError(const Result<T>& r) noexcept {
  return std::holds_alternative<ParseError>(r);
}

template <typename T>
[[nodiscard]] inline const T& GetValue(const Result<T>& r) noexcept {
  return std::get<T>(r);
}

template <typename T>
[[nodiscard]] inline ParseError GetError(const Result<T>& r) noexcept {
  return std::get<ParseError>(r);
}

// ═══════════════════════════════════════════════════════════════════════════
// Структуры данных
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Телеметрия от MCU к ESP32.
 * Payload: 15 байт (seq:2 + status:1 + ax:2 + ay:2 + az:2 + gx:2 + gy:2 + gz:2)
 */
struct TelemetryData {
  uint16_t seq{0};
  uint8_t status{0};  // bit0: rc_ok, bit1: wifi_ok, bit2: failsafe_active
  int16_t ax{0}, ay{0}, az{0};  // Акселерометр (mg)
  int16_t gx{0}, gy{0}, gz{0};  // Гироскоп (mdps)

  static constexpr size_t PAYLOAD_SIZE = 15;

  // Методы для работы с битовыми флагами
  [[nodiscard]] bool IsRcOk() const noexcept { return status & 0x01; }
  [[nodiscard]] bool IsWifiOk() const noexcept { return status & 0x02; }
  [[nodiscard]] bool IsFailsafeActive() const noexcept { return status & 0x04; }

  void SetRcOk(bool ok) noexcept {
    status = ok ? (status | 0x01) : (status & ~0x01);
  }
  void SetWifiOk(bool ok) noexcept {
    status = ok ? (status | 0x02) : (status & ~0x02);
  }
  void SetFailsafeActive(bool active) noexcept {
    status = active ? (status | 0x04) : (status & ~0x04);
  }
};

/**
 * Команда от ESP32 к MCU.
 * Payload: 7 байт (seq:2 + throttle:2 + steering:2 + reserved:1)
 */
struct CommandData {
  uint16_t seq{0};
  float throttle{0.0f};  // [-1.0, 1.0]
  float steering{0.0f};  // [-1.0, 1.0]

  static constexpr size_t PAYLOAD_SIZE = 7;

  // Возвращает команду с ограниченными значениями [-1.0, 1.0]
  [[nodiscard]] CommandData Clamped() const noexcept {
    auto clamp = [](float val) -> float {
      if (val > 1.0f) return 1.0f;
      if (val < -1.0f) return -1.0f;
      return val;
    };
    return CommandData{seq, clamp(throttle), clamp(steering)};
  }
};

// ═══════════════════════════════════════════════════════════════════════════
// Вспомогательные классы
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Построитель кадров протокола.
 * Инкапсулирует общую логику создания заголовка и CRC.
 */
class FrameBuilder {
 public:
  explicit FrameBuilder(MessageType type) noexcept : type_(type) {}

  /**
   * Построить кадр с заданным payload.
   * @param buffer Буфер для записи кадра (минимум HEADER_SIZE + payload.size()
   * + CRC_SIZE)
   * @param payload Данные payload
   * @return Размер кадра или ошибка
   */
  [[nodiscard]] Result<size_t> Build(
      std::span<uint8_t> buffer,
      std::span<const uint8_t> payload) const noexcept;

 private:
  MessageType type_;

  void WriteHeader(std::span<uint8_t> buffer,
                   uint16_t payload_len) const noexcept;
  void WriteCrc(std::span<uint8_t> buffer, size_t payload_len) const noexcept;
};

/**
 * Парсер кадров протокола.
 * Инкапсулирует общую логику валидации заголовка и CRC.
 */
class FrameParser {
 public:
  /**
   * Валидировать заголовок кадра и вернуть тип сообщения.
   * @param buffer Буфер с данными (минимум 4 байта)
   * @return Тип сообщения или ошибка
   */
  [[nodiscard]] static Result<MessageType> ValidateHeader(
      std::span<const uint8_t> buffer) noexcept;

  /**
   * Получить длину payload из заголовка.
   * @param buffer Буфер с данными (минимум HEADER_SIZE байт)
   * @return Длина payload или ошибка
   */
  [[nodiscard]] static Result<uint16_t> GetPayloadLength(
      std::span<const uint8_t> buffer) noexcept;

  /**
   * Проверить CRC кадра.
   * @param buffer Буфер с полным кадром
   * @return true если CRC корректна
   */
  [[nodiscard]] static bool ValidateCrc(
      std::span<const uint8_t> buffer) noexcept;

  /**
   * Найти начало кадра (AA 55) в буфере.
   * @param buffer Буфер для поиска
   * @return Индекс начала кадра или -1 если не найден
   */
  [[nodiscard]] static int FindFrameStart(
      std::span<const uint8_t> buffer) noexcept;
};

// ═══════════════════════════════════════════════════════════════════════════
// Основной API протокола
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Класс для сериализации и десериализации сообщений протокола.
 * Инкапсулирует счётчик последовательности команд.
 */
class Protocol {
 public:
  // ─────────────────────────────────────────────────────────────────────────
  // Сборка кадров
  // ─────────────────────────────────────────────────────────────────────────

  /**
   * Построить кадр телеметрии (MCU → ESP32).
   * @param buffer Буфер для записи (минимум 23 байта)
   * @param data Данные телеметрии
   * @return Размер кадра или ошибка
   */
  [[nodiscard]] static Result<size_t> BuildTelemetry(
      std::span<uint8_t> buffer, const TelemetryData& data) noexcept;

  /**
   * Построить кадр команды (ESP32 → MCU).
   * @param buffer Буфер для записи (минимум 15 байт)
   * @param data Данные команды
   * @return Размер кадра или ошибка
   */
  [[nodiscard]] static Result<size_t> BuildCommand(
      std::span<uint8_t> buffer, const CommandData& data) noexcept;

  /**
   * Построить кадр лога (MCU → ESP32).
   * @param buffer Буфер для записи
   * @param msg Текст сообщения (обрезается до LOG_MAX_PAYLOAD)
   * @return Размер кадра или ошибка
   */
  [[nodiscard]] static Result<size_t> BuildLog(std::span<uint8_t> buffer,
                                               std::string_view msg) noexcept;

  /**
   * Построить кадр PING (ESP32 → MCU).
   * @param buffer Буфер для записи (минимум 8 байт)
   * @return Размер кадра или ошибка
   */
  [[nodiscard]] static Result<size_t> BuildPing(
      std::span<uint8_t> buffer) noexcept;

  /**
   * Построить кадр PONG (MCU → ESP32).
   * @param buffer Буфер для записи (минимум 8 байт)
   * @return Размер кадра или ошибка
   */
  [[nodiscard]] static Result<size_t> BuildPong(
      std::span<uint8_t> buffer) noexcept;

  // ─────────────────────────────────────────────────────────────────────────
  // Парсинг кадров
  // ─────────────────────────────────────────────────────────────────────────

  /**
   * Распарсить кадр телеметрии (ESP32 принимает от MCU).
   * @param buffer Буфер с данными
   * @return Данные телеметрии или ошибка
   */
  [[nodiscard]] static Result<TelemetryData> ParseTelemetry(
      std::span<const uint8_t> buffer) noexcept;

  /**
   * Распарсить кадр команды (MCU принимает от ESP32).
   * @param buffer Буфер с данными
   * @return Данные команды или ошибка
   */
  [[nodiscard]] static Result<CommandData> ParseCommand(
      std::span<const uint8_t> buffer) noexcept;

  /**
   * Распарсить кадр лога (ESP32 принимает от MCU).
   * @param buffer Буфер с данными
   * @return Текст сообщения или ошибка
   */
  [[nodiscard]] static Result<std::string_view> ParseLog(
      std::span<const uint8_t> buffer) noexcept;

  /**
   * Распарсить кадр PING (MCU принимает от ESP32).
   * @param buffer Буфер с данными
   * @return void или ошибка (используйте IsOk для проверки)
   */
  [[nodiscard]] static Result<bool> ParsePing(
      std::span<const uint8_t> buffer) noexcept;

  /**
   * Распарсить кадр PONG (ESP32 принимает от MCU).
   * @param buffer Буфер с данными
   * @return void или ошибка (используйте IsOk для проверки)
   */
  [[nodiscard]] static Result<bool> ParsePong(
      std::span<const uint8_t> buffer) noexcept;

  // ─────────────────────────────────────────────────────────────────────────
  // Утилиты
  // ─────────────────────────────────────────────────────────────────────────

  /**
   * Вычислить CRC16 для данных.
   * @param data Данные для вычисления CRC
   * @return CRC16
   */
  [[nodiscard]] static uint16_t CalculateCrc16(
      std::span<const uint8_t> data) noexcept;

  /**
   * Найти начало кадра в буфере.
   * @param buffer Буфер для поиска
   * @return Индекс начала кадра или -1
   */
  [[nodiscard]] static int FindFrameStart(
      std::span<const uint8_t> buffer) noexcept {
    return FrameParser::FindFrameStart(buffer);
  }

 private:
  static uint16_t next_command_seq_;  // Счётчик последовательности команд
};

}  // namespace rc_vehicle::protocol
