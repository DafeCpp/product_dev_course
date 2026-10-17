#include "snapshot_json.hpp"

#include <charconv>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <limits>

namespace bench {

namespace {

/// Дописывающий в буфер писатель; при нехватке места помечает overflow
class JsonWriter {
 public:
  JsonWriter(char* buf, size_t cap) : buf_(buf), cap_(cap) {}

  void Raw(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    if (overflow_) return;
    va_list args;
    va_start(args, fmt);
    const int n = std::vsnprintf(buf_ + len_, cap_ - len_, fmt, args);
    va_end(args);
    if (n < 0 || static_cast<size_t>(n) >= cap_ - len_) {
      overflow_ = true;
      return;
    }
    len_ += static_cast<size_t>(n);
  }

  /// Число с тремя знаками; NaN/inf → null (в JSON их нет)
  void Float(const char* key, float v) {
    if (std::isfinite(v)) {
      Raw("\"%s\":%.3f", key, static_cast<double>(v));
    } else {
      Raw("\"%s\":null", key);
    }
  }

  [[nodiscard]] size_t Finish() const { return overflow_ ? 0 : len_; }

 private:
  char* buf_;
  size_t cap_;
  size_t len_{0};
  bool overflow_{false};
};

const char* Bool(bool v) { return v ? "true" : "false"; }

void WriteAck(JsonWriter& w, const CommandAck& a) {
  w.Raw(
      "{\"cmd_id\":%u,\"cmd\":\"%s\",\"result\":\"%s\",\"reason\":\"%s\","
      "\"duplicate\":%s}",
      static_cast<unsigned>(a.cmd_id), ToString(a.type), ToString(a.result),
      ToString(a.reason), Bool(a.duplicate));
}

void WriteJournalEntry(JsonWriter& w, const JournalEntry& e) {
  w.Raw("{\"seq\":%u,\"t_ms\":%u,\"kind\":\"%s\"", static_cast<unsigned>(e.seq),
        static_cast<unsigned>(e.t_ms), ToString(e.kind));
  switch (e.kind) {
    case JournalKind::kCommand:
      w.Raw(
          ",\"cmd_id\":%u,\"cmd\":\"%s\",\"result\":\"%s\",\"reason\":\"%s\","
          "\"duplicate\":%s",
          static_cast<unsigned>(e.cmd_id),
          ToString(static_cast<CommandType>(e.arg0)),
          ToString(static_cast<CommandResult>(e.arg1)),
          ToString(static_cast<RejectReason>(e.arg2)),
          Bool((e.flags & 1) != 0));
      break;
    case JournalKind::kLinkState:
      w.Raw(",\"from\":\"%s\",\"to\":\"%s\"",
            ToString(static_cast<LinkState>(e.arg0)),
            ToString(static_cast<LinkState>(e.arg1)));
      break;
    case JournalKind::kHold:
      w.Raw(",\"reason\":\"%s\"", ToString(static_cast<StopReason>(e.arg0)));
      break;
    case JournalKind::kFailureLatch:
      break;
  }
  w.Raw("}");
}

}  // namespace

const char* ToString(CommandType t) noexcept {
  switch (t) {
    case CommandType::kEnable:
      return "enable";
    case CommandType::kSetMode:
      return "set_mode";
    case CommandType::kSetSetpoint:
      return "set_setpoint";
    case CommandType::kControlledStop:
      return "controlled_stop";
    case CommandType::kEStopSoft:
      return "e_stop_soft";
  }
  return "unknown";
}

const char* ToString(CommandResult r) noexcept {
  return r == CommandResult::kAccepted ? "accepted" : "rejected";
}

const char* ToString(RejectReason r) noexcept {
  switch (r) {
    case RejectReason::kNone:
      return "none";
    case RejectReason::kInvalidId:
      return "invalid_id";
    case RejectReason::kProgramRunning:
      return "program_running";
    case RejectReason::kHeld:
      return "held";
    case RejectReason::kStopped:
      return "stopped";
    case RejectReason::kOutOfRange:
      return "out_of_range";
  }
  return "unknown";
}

const char* ToString(ControlMode m) noexcept {
  return m == ControlMode::kForce ? "force" : "displacement";
}

const char* ToString(LinkState s) noexcept {
  switch (s) {
    case LinkState::kRunning:
      return "running";
    case LinkState::kGracePeriod:
      return "grace";
    case LinkState::kRampDown:
      return "ramp_down";
    case LinkState::kSafeHold:
      return "safe_hold";
  }
  return "unknown";
}

const char* ToString(StopReason r) noexcept {
  switch (r) {
    case StopReason::kNone:
      return "none";
    case StopReason::kOperatorControlledStop:
      return "operator_controlled_stop";
    case StopReason::kOperatorEStopSoft:
      return "operator_e_stop_soft";
    case StopReason::kLinkSafeHold:
      return "link_safe_hold";
    case StopReason::kSpecimenFailure:
      return "specimen_failure";
  }
  return "unknown";
}

const char* ToString(EstopChainState s) noexcept {
  switch (s) {
    case EstopChainState::kUnknown:
      return "unknown";
    case EstopChainState::kOk:
      return "ok";
    case EstopChainState::kOpen:
      return "open";
  }
  return "unknown";
}

const char* ToString(JournalKind k) noexcept {
  switch (k) {
    case JournalKind::kCommand:
      return "command";
    case JournalKind::kLinkState:
      return "link_state";
    case JournalKind::kFailureLatch:
      return "failure_latch";
    case JournalKind::kHold:
      return "hold";
  }
  return "unknown";
}

const char* ToString(ParseError e) noexcept {
  switch (e) {
    case ParseError::kOk:
      return "ok";
    case ParseError::kSyntax:
      return "syntax";
    case ParseError::kUnknownKey:
      return "unknown_key";
    case ParseError::kUnknownType:
      return "unknown_type";
    case ParseError::kMissingField:
      return "missing_field";
    case ParseError::kBadValue:
      return "bad_value";
  }
  return "unknown";
}

size_t BuildSnapshotJson(const BenchStateSnapshot& s, char* buf, size_t cap) {
  if (cap == 0) return 0;
  JsonWriter w(buf, cap);
  w.Raw(
      "{\"v\":1,\"type\":\"snapshot\",\"seq\":%u,\"uptime_ms\":%u,"
      "\"mode\":\"%s\",\"enabled\":%s,\"link_state\":\"%s\",\"holding\":%s,"
      "\"stop_reason\":\"%s\",\"program_active\":%s,\"failure_latched\":%s,"
      "\"estop_chain\":\"%s\",",
      static_cast<unsigned>(s.seq), static_cast<unsigned>(s.uptime_ms),
      ToString(s.mode), Bool(s.enabled), ToString(s.link_state),
      Bool(s.holding), ToString(s.last_stop_reason), Bool(s.program_active),
      Bool(s.failure_latched), ToString(s.estop_chain));

  w.Raw("\"setpoint\":{");
  w.Float("program", s.program_target);
  w.Raw(",");
  w.Float("commanded", s.manual_commanded);
  w.Raw(",");
  w.Float("ramped", s.manual_ramped);
  w.Raw(",");
  w.Float("effective", s.effective_target);
  w.Raw("},");
  w.Float("valve_command", s.valve_command);

  w.Raw(",\"feedback\":{");
  w.Float("force_n", s.force_n);
  w.Raw(",");
  w.Float("position_mm", s.position_mm);
  w.Raw(",\"status\":%u,\"fresh\":%s,\"age_ticks\":%u},",
        static_cast<unsigned>(s.valve_status), Bool(s.fb_fresh),
        static_cast<unsigned>(s.fb_age_ticks));

  w.Raw(
      "\"counters\":{\"cmd_total\":%u,\"cmd_rejected\":%u,"
      "\"cmd_duplicate\":%u,\"cmd_overwritten\":%u},",
      static_cast<unsigned>(s.cmd_total), static_cast<unsigned>(s.cmd_rejected),
      static_cast<unsigned>(s.cmd_duplicate),
      static_cast<unsigned>(s.cmd_overwritten));

  w.Raw("\"acks\":[");
  for (uint8_t i = 0; i < s.ack_count; ++i) {
    if (i != 0) w.Raw(",");
    WriteAck(w, s.acks[i]);
  }

  w.Raw(
      "],\"tick_us\":{\"count\":%u,\"min\":%u,\"avg\":%u,\"p99\":%u,"
      "\"max\":%u},\"journal_total\":%u,\"journal\":[",
      static_cast<unsigned>(s.tick.count), static_cast<unsigned>(s.tick.min_us),
      static_cast<unsigned>(s.tick.avg_us),
      static_cast<unsigned>(s.tick.p99_us),
      static_cast<unsigned>(s.tick.max_us),
      static_cast<unsigned>(s.journal_total));
  for (uint8_t i = 0; i < s.journal_count; ++i) {
    if (i != 0) w.Raw(",");
    WriteJournalEntry(w, s.journal[i]);
  }
  w.Raw("]}");
  return w.Finish();
}

size_t BuildAckJson(const CommandAck& ack, char* buf, size_t cap) {
  if (cap == 0) return 0;
  JsonWriter w(buf, cap);
  w.Raw(
      "{\"v\":1,\"type\":\"ack\",\"cmd_id\":%u,\"cmd\":\"%s\","
      "\"result\":\"%s\",\"reason\":\"%s\",\"duplicate\":%s}",
      static_cast<unsigned>(ack.cmd_id), ToString(ack.type),
      ToString(ack.result), ToString(ack.reason), Bool(ack.duplicate));
  return w.Finish();
}

// ---------------------------------------------------------------------------
// Разбор команды: плоский объект, строгий набор ключей, без экранирования
// ---------------------------------------------------------------------------

namespace {

class Cursor {
 public:
  explicit Cursor(std::string_view s) : s_(s) {}

  void SkipWs() {
    while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' ||
                                s_[pos_] == '\n' || s_[pos_] == '\r')) {
      ++pos_;
    }
  }
  bool Eat(char c) {
    SkipWs();
    if (pos_ < s_.size() && s_[pos_] == c) {
      ++pos_;
      return true;
    }
    return false;
  }
  bool Peek(char c) {
    SkipWs();
    return pos_ < s_.size() && s_[pos_] == c;
  }
  bool AtEnd() {
    SkipWs();
    return pos_ == s_.size();
  }
  /// Строка без escape-последовательностей
  bool String(std::string_view& out) {
    if (!Eat('"')) return false;
    const size_t start = pos_;
    while (pos_ < s_.size() && s_[pos_] != '"') {
      if (s_[pos_] == '\\') return false;
      ++pos_;
    }
    if (pos_ >= s_.size()) return false;
    out = s_.substr(start, pos_ - start);
    ++pos_;
    return true;
  }
  bool Literal(std::string_view lit) {
    SkipWs();
    if (s_.substr(pos_, lit.size()) == lit) {
      pos_ += lit.size();
      return true;
    }
    return false;
  }
  /// Токен числа: до разделителя
  std::string_view NumberToken() {
    SkipWs();
    const size_t start = pos_;
    while (pos_ < s_.size() && s_[pos_] != ',' && s_[pos_] != '}' &&
           s_[pos_] != ' ' && s_[pos_] != '\t' && s_[pos_] != '\n' &&
           s_[pos_] != '\r') {
      ++pos_;
    }
    return s_.substr(start, pos_ - start);
  }

 private:
  std::string_view s_;
  size_t pos_{0};
};

}  // namespace

ParseError ParseCommandJson(std::string_view json, BenchCommand& out) {
  Cursor c(json);
  BenchCommand cmd{};
  bool has_id = false, has_type = false, has_mode = false, has_value = false,
       has_enable = false;

  if (!c.Eat('{')) return ParseError::kSyntax;
  if (!c.Peek('}')) {
    while (true) {
      std::string_view key;
      if (!c.String(key) || !c.Eat(':')) return ParseError::kSyntax;

      if (key == "cmd_id") {
        const std::string_view tok = c.NumberToken();
        uint32_t id = 0;
        const auto r = std::from_chars(tok.data(), tok.data() + tok.size(), id);
        if (tok.empty() || r.ec != std::errc{} ||
            r.ptr != tok.data() + tok.size()) {
          return ParseError::kBadValue;
        }
        cmd.cmd_id = id;
        has_id = true;
      } else if (key == "type") {
        std::string_view v;
        if (!c.String(v)) return ParseError::kBadValue;
        if (v == "enable")
          cmd.type = CommandType::kEnable;
        else if (v == "set_mode")
          cmd.type = CommandType::kSetMode;
        else if (v == "set_setpoint")
          cmd.type = CommandType::kSetSetpoint;
        else if (v == "controlled_stop")
          cmd.type = CommandType::kControlledStop;
        else if (v == "e_stop_soft")
          cmd.type = CommandType::kEStopSoft;
        else
          return ParseError::kUnknownType;
        has_type = true;
      } else if (key == "mode") {
        std::string_view v;
        if (!c.String(v)) return ParseError::kBadValue;
        if (v == "force")
          cmd.mode = ControlMode::kForce;
        else if (v == "displacement")
          cmd.mode = ControlMode::kDisplacement;
        else
          return ParseError::kUnknownType;
        has_mode = true;
      } else if (key == "value") {
        const std::string_view tok = c.NumberToken();
        float v = 0.0f;
        const auto r = std::from_chars(tok.data(), tok.data() + tok.size(), v);
        if (tok.empty() || r.ec != std::errc{} ||
            r.ptr != tok.data() + tok.size()) {
          return ParseError::kBadValue;
        }
        cmd.value = v;
        has_value = true;
      } else if (key == "enable") {
        if (c.Literal("true"))
          cmd.enable = true;
        else if (c.Literal("false"))
          cmd.enable = false;
        else
          return ParseError::kBadValue;
        has_enable = true;
      } else {
        return ParseError::kUnknownKey;
      }

      if (c.Eat(',')) continue;
      break;
    }
  }
  if (!c.Eat('}') || !c.AtEnd()) return ParseError::kSyntax;

  if (!has_id || !has_type) return ParseError::kMissingField;
  if (cmd.type == CommandType::kEnable && !has_enable)
    return ParseError::kMissingField;
  if (cmd.type == CommandType::kSetMode && !has_mode)
    return ParseError::kMissingField;
  if (cmd.type == CommandType::kSetSetpoint && !has_value)
    return ParseError::kMissingField;

  out = cmd;
  return ParseError::kOk;
}

}  // namespace bench
