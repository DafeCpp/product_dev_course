#include "vesc_protocol.hpp"

namespace rc_vehicle::vesc {

namespace {

constexpr uint8_t kStartShort = 0x02;
constexpr uint8_t kStartLong = 0x03;
constexpr uint8_t kEnd = 0x03;

/// Обязательная часть ответа GET_VALUES: id + поля до fault_code включительно.
constexpr size_t kGetValuesMinSize =
    1 + 2 + 2 + 4 * 4 + 2 + 4 + 2 + 4 * 4 + 4 + 4 + 1;

class Reader {
 public:
  explicit Reader(std::span<const uint8_t> data) : data_(data) {}

  int16_t I16() noexcept {
    const auto v = static_cast<int16_t>((data_[pos_] << 8) | data_[pos_ + 1]);
    pos_ += 2;
    return v;
  }

  int32_t I32() noexcept {
    const uint32_t v = (static_cast<uint32_t>(data_[pos_]) << 24) |
                       (static_cast<uint32_t>(data_[pos_ + 1]) << 16) |
                       (static_cast<uint32_t>(data_[pos_ + 2]) << 8) |
                       static_cast<uint32_t>(data_[pos_ + 3]);
    pos_ += 4;
    return static_cast<int32_t>(v);
  }

  uint8_t U8() noexcept { return data_[pos_++]; }

  float F16(float scale) noexcept { return static_cast<float>(I16()) / scale; }
  float F32(float scale) noexcept { return static_cast<float>(I32()) / scale; }

  void Skip(size_t n) noexcept { pos_ += n; }

 private:
  std::span<const uint8_t> data_;
  size_t pos_{0};
};

}  // namespace

uint16_t Crc16(std::span<const uint8_t> data) noexcept {
  uint16_t crc = 0;
  for (uint8_t b : data) {
    crc ^= static_cast<uint16_t>(b) << 8;
    for (int i = 0; i < 8; ++i) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                           : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

size_t EncodePacket(std::span<const uint8_t> payload,
                    std::span<uint8_t> out) noexcept {
  const size_t len = payload.size();
  // Длинный формат (len > 255) нам не нужен: запросы короткие.
  if (len == 0 || len > 255 || len > kMaxPayload) return 0;
  if (out.size() < FrameSize(len)) return 0;

  size_t i = 0;
  out[i++] = kStartShort;
  out[i++] = static_cast<uint8_t>(len);
  for (uint8_t b : payload) out[i++] = b;
  const uint16_t crc = Crc16(payload);
  out[i++] = static_cast<uint8_t>(crc >> 8);
  out[i++] = static_cast<uint8_t>(crc & 0xFF);
  out[i++] = kEnd;
  return i;
}

size_t BuildGetValuesRequest(std::span<uint8_t> out) noexcept {
  const uint8_t payload[] = {kCommGetValues};
  return EncodePacket(payload, out);
}

void PacketParser::Fail() noexcept {
  ++errors_;
  state_ = State::Start;
}

std::optional<std::span<const uint8_t>> PacketParser::Feed(
    uint8_t byte) noexcept {
  switch (state_) {
    case State::Start:
      if (byte == kStartShort) {
        len_ = 0;
        state_ = State::LenLow;
      } else if (byte == kStartLong) {
        len_ = 0;
        state_ = State::LenHigh;
      }
      // Остальное — мусор между кадрами, молча пропускаем.
      break;
    case State::LenHigh:
      len_ = static_cast<size_t>(byte) << 8;
      state_ = State::LenLow;
      break;
    case State::LenLow:
      len_ |= byte;
      if (len_ == 0 || len_ > kMaxPayload) {
        Fail();
      } else {
        pos_ = 0;
        state_ = State::Payload;
      }
      break;
    case State::Payload:
      buf_[pos_++] = byte;
      if (pos_ == len_) state_ = State::CrcHigh;
      break;
    case State::CrcHigh:
      crc_ = static_cast<uint16_t>(byte) << 8;
      state_ = State::CrcLow;
      break;
    case State::CrcLow:
      crc_ |= byte;
      state_ = State::End;
      break;
    case State::End: {
      const std::span<const uint8_t> payload(buf_.data(), len_);
      if (byte != kEnd || Crc16(payload) != crc_) {
        Fail();
        break;
      }
      state_ = State::Start;
      return payload;
    }
  }
  return std::nullopt;
}

std::optional<EscTelemetry> ParseGetValues(
    std::span<const uint8_t> payload) noexcept {
  if (payload.size() < kGetValuesMinSize || payload[0] != kCommGetValues) {
    return std::nullopt;
  }

  Reader r(payload);
  r.Skip(1);  // id команды
  EscTelemetry t{};
  t.temp_fet_c = r.F16(10.0f);
  t.temp_motor_c = r.F16(10.0f);
  t.motor_current_a = r.F32(100.0f);
  t.input_current_a = r.F32(100.0f);
  r.Skip(4 + 4);  // avg_id, avg_iq
  t.duty = r.F16(1000.0f);
  t.erpm = r.F32(1.0f);
  t.input_voltage_v = r.F16(10.0f);
  r.Skip(4 * 4);  // amp_hours, amp_hours_charged, watt_hours, *_charged
  t.tachometer = r.I32();
  r.Skip(4);  // tachometer_abs
  t.fault_code = r.U8();
  return t;
}

}  // namespace rc_vehicle::vesc
