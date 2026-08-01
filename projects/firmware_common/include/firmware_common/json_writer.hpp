#pragma once

#include <cmath>
#include <cstdint>
#include <string>

namespace firmware_common {

/**
 * Пишет плоский/вложенный JSON в переданный std::string без cJSON.
 *
 * LOS-252: cJSON_PrintUnformatted печатает каждое число через
 * sprintf("%1.15g") -> sscanf (round-trip check) -> обычно повторный
 * sprintf("%1.17g") — три вызова softfloat-libc на поле, плюс ~2 malloc на
 * узел дерева. На телеметрическом кадре (~80 полей, 20 кадров/с) это ~27 мс
 * CPU на кадр и, через общий для обоих ядер кеш флеша, заметно замедляет
 * control loop на другом ядре.
 *
 * JsonWriter пишет числа только целочисленной арифметикой (Fixed —
 * llroundf + деление/остаток, без printf/double) и без аллокаций дерева —
 * один reserve() на весь кадр со стороны вызывающего кода. Формат вывода —
 * тот же JSON с теми же ключами, просто с разумной, а не 17-значной
 * точностью.
 *
 * Не потокобезопасен, без RTTI/исключений — подходит для xtensa newlib.
 */
class JsonWriter {
 public:
  explicit JsonWriter(std::string& out) : out_(out) {}

  /** Открыть корневой объект (без ключа). */
  void BeginObject() {
    out_ += '{';
    PushLevel();
  }

  void BeginObject(const char* key) {
    WriteKey(key);
    out_ += '{';
    PushLevel();
  }

  void EndObject() {
    out_ += '}';
    PopLevel();
  }

  void BeginArray(const char* key) {
    WriteKey(key);
    out_ += '[';
    PushLevel();
  }

  void EndArray() {
    out_ += ']';
    PopLevel();
  }

  void Bool(const char* key, bool v) {
    WriteKey(key);
    out_ += v ? "true" : "false";
  }

  void Int(const char* key, int64_t v) {
    WriteKey(key);
    AppendInt(v);
  }

  /**
   * Строковое значение как есть, без экранирования.
   * Только для ASCII-литералов без спецсимволов (статусы, "telem" и т.п.).
   */
  void RawStr(const char* key, const char* value) {
    WriteKey(key);
    out_ += '"';
    out_ += value;
    out_ += '"';
  }

  /**
   * Число с фиксированной точностью (decimals: 0..6 — верхняя граница
   * задана вызовом AppendFixedValue(v, 6) из Sci(), а не прямым
   * использованием). NaN/Inf и выход за int32 после масштабирования -> null
   * (как у cJSON). Хвостовые нули дробной части обрезаются: 0.000 -> "0", 0.120
   * -> "0.12".
   */
  void Fixed(const char* key, float v, int decimals) {
    WriteKey(key);
    AppendFixedValue(v, decimals);
  }

  /** Элемент числового массива с фиксированной точностью, без ключа. */
  void FixedElem(float v, int decimals) {
    CommaIfNeeded();
    AppendFixedValue(v, decimals);
  }

  /**
   * Научная нотация (мантисса.1 цифра + экспонента) для величин < 1e-4 по
   * модулю (типично — дисперсии EKF); иначе — Fixed(..., 6). Клиент парсит
   * JSON в обычное число и форматирует сам (JS toExponential не зависит от
   * того, как число записано на проводе), так что смешанное представление
   * на проводе не проблема.
   */
  void Sci(const char* key, float v) {
    WriteKey(key);
    AppendSciValue(v);
  }

 private:
  static constexpr int kMaxDepth = 8;

  void PushLevel() {
    ++depth_;
    if (depth_ < kMaxDepth) {
      first_[depth_] = true;
    }
  }

  void PopLevel() {
    if (depth_ > 0) --depth_;
  }

  void CommaIfNeeded() {
    if (depth_ >= kMaxDepth) return;
    if (first_[depth_]) {
      first_[depth_] = false;
    } else {
      out_ += ',';
    }
  }

  void WriteKey(const char* key) {
    CommaIfNeeded();
    out_ += '"';
    out_ += key;
    out_ += "\":";
  }

  void AppendUInt(uint64_t v) {
    if (v == 0) {
      out_ += '0';
      return;
    }
    char buf[20];
    int len = 0;
    while (v > 0) {
      buf[len++] = static_cast<char>('0' + (v % 10));
      v /= 10;
    }
    while (len > 0) out_ += buf[--len];
  }

  void AppendInt(int64_t v) {
    if (v < 0) {
      out_ += '-';
      AppendUInt(static_cast<uint64_t>(-v));
    } else {
      AppendUInt(static_cast<uint64_t>(v));
    }
  }

  void AppendFixedValue(float v, int decimals) {
    if (!std::isfinite(v)) {
      out_ += "null";
      return;
    }
    static constexpr int64_t kScale[7] = {1,     10,     100,    1000,
                                          10000, 100000, 1000000};
    const int64_t scale = kScale[decimals];
    const float scaled_f = v * static_cast<float>(scale);
    if (scaled_f > 2147483647.0f || scaled_f < -2147483647.0f) {
      out_ += "null";
      return;
    }
    int64_t scaled = std::llround(scaled_f);
    if (scaled == 0) {
      out_ += '0';
      return;
    }
    if (scaled < 0) {
      out_ += '-';
      scaled = -scaled;
    }
    const int64_t int_part = scaled / scale;
    int64_t frac_part = scaled % scale;
    AppendUInt(static_cast<uint64_t>(int_part));
    if (frac_part != 0) {
      out_ += '.';
      char buf[8];
      for (int i = decimals - 1; i >= 0; --i) {
        buf[i] = static_cast<char>('0' + (frac_part % 10));
        frac_part /= 10;
      }
      int len = decimals;
      while (len > 0 && buf[len - 1] == '0') --len;
      out_.append(buf, static_cast<size_t>(len));
    }
  }

  void AppendSciValue(float v) {
    if (!std::isfinite(v)) {
      out_ += "null";
      return;
    }
    if (v == 0.0f) {
      out_ += '0';
      return;
    }
    const float av = std::fabs(v);
    if (av >= 1e-4f) {
      AppendFixedValue(v, 6);
      return;
    }
    int exp = static_cast<int>(std::floor(std::log10(av)));
    float mantissa = av / std::pow(10.0f, static_cast<float>(exp));
    int64_t mantissa10 = std::llround(mantissa * 10.0f);
    if (mantissa10 >= 100) {
      mantissa10 /= 10;
      ++exp;
    }
    if (v < 0) out_ += '-';
    out_ += static_cast<char>('0' + (mantissa10 / 10));
    out_ += '.';
    out_ += static_cast<char>('0' + (mantissa10 % 10));
    out_ += 'e';
    out_ += (exp < 0) ? '-' : '+';
    AppendUInt(static_cast<uint64_t>(exp < 0 ? -exp : exp));
  }

  std::string& out_;
  int depth_ = 0;
  bool first_[kMaxDepth] = {};
};

}  // namespace firmware_common
