#pragma once

#include <iomanip>
#include <sstream>
#include <string>

namespace rc_vehicle {

/**
 * @brief Простой форматтер для логирования без использования snprintf
 *
 * Использует std::ostringstream для безопасного форматирования строк.
 * Примеры:
 *   LogFormat() << "Value: " << 42 << ", float: " << 3.14f;
 *   LogFormat() << "Hex: 0x" << std::hex << std::setw(2) << std::setfill('0')
 * << value;
 */
class LogFormat {
 public:
  LogFormat() = default;

  template <typename T>
  LogFormat& operator<<(const T& value) {
    stream_ << value;
    return *this;
  }

  // Поддержка манипуляторов потока (std::hex, std::setw, и т.д.)
  LogFormat& operator<<(std::ostream& (*manip)(std::ostream&)) {
    manip(stream_);
    return *this;
  }

  template <typename T>
  LogFormat& operator<<(T& (*manip)(T&)) {
    manip(stream_);
    return *this;
  }

  std::string str() const { return stream_.str(); }

  operator std::string() const { return str(); }

 private:
  std::ostringstream stream_;
};

/**
 * @brief Форматирование IP-адреса (для ESP32 IPSTR макроса)
 * @param ip Указатель на структуру esp_ip4_addr_t или uint32_t IP
 * @return Строка вида "192.168.4.1"
 */
inline std::string FormatIp(uint32_t ip) {
  LogFormat fmt;
  fmt << ((ip >> 0) & 0xFF) << "." << ((ip >> 8) & 0xFF) << "."
      << ((ip >> 16) & 0xFF) << "." << ((ip >> 24) & 0xFF);
  return fmt.str();
}

}  // namespace rc_vehicle