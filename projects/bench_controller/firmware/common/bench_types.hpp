#pragma once

#include <cstdint>

namespace bench {

/**
 * @brief Режим регулирования канала нагружения
 *
 * Значения совпадают с полем mode контрольного слова PDO (биты 0–1),
 * см. docs/experiment-control/design-notes.md §4.
 */
enum class ControlMode : uint8_t {
  kForce = 0,         ///< Контур по усилию (датчик силы)
  kDisplacement = 1,  ///< Контур по перемещению (LVDT)
};

/**
 * @brief Состояние связи с supervisory-уровнем (платформой)
 *
 * Политика по итогам discovery (LOS-67): при потере связи MCU не
 * продолжает программу автономно — grace-период, затем плавная
 * разгрузка до безопасного состояния.
 */
enum class LinkState : uint8_t {
  kRunning = 0,  ///< Связь есть, программа исполняется
  kGracePeriod,  ///< Связь потеряна, ждём восстановления
  kRampDown,     ///< Плавная разгрузка (амплитуда → 0)
  kSafeHold,     ///< Безопасное удержание (displacement-hold)
};

/**
 * @brief Команда клапану (выход контура)
 */
struct ValveSetpoint {
  ControlMode mode{ControlMode::kDisplacement};
  float value{0.0f};   ///< Нормированная команда клапану, [-1..1]
  bool enable{false};  ///< Разрешение работы (бит 7 контрольного слова)
};

/**
 * @brief Обратная связь от электроники клапана (TPDO)
 */
struct ValveFeedback {
  float force_n{0.0f};      ///< Усилие, Н
  float position_mm{0.0f};  ///< Перемещение штока, мм
  uint8_t status{0};        ///< Статусное слово узла
  bool fresh{false};        ///< Пришёл ли новый кадр с прошлого тика
  uint32_t age_ticks{0};    ///< Возраст данных в тиках контура
};

/**
 * @brief Масштабы физических величин для int16-полей PDO
 *
 * ±32767 квантов = ±полный диапазон. Значения диапазонов — рабочие
 * для спайка; для продакшена берутся из паспорта стенда.
 */
struct PdoScaling {
  static constexpr float kFullScaleForceN = 100'000.0f;  // ±100 кН
  static constexpr float kFullScalePositionMm = 100.0f;  // ±100 мм
  static constexpr float kRawFullScale = 32767.0f;

  static constexpr int16_t ForceToRaw(float n) noexcept {
    return Saturate(n / kFullScaleForceN * kRawFullScale);
  }
  static constexpr float RawToForce(int16_t raw) noexcept {
    return static_cast<float>(raw) / kRawFullScale * kFullScaleForceN;
  }
  static constexpr int16_t PositionToRaw(float mm) noexcept {
    return Saturate(mm / kFullScalePositionMm * kRawFullScale);
  }
  static constexpr float RawToPosition(int16_t raw) noexcept {
    return static_cast<float>(raw) / kRawFullScale * kFullScalePositionMm;
  }
  static constexpr int16_t CommandToRaw(float cmd) noexcept {
    return Saturate(cmd * kRawFullScale);
  }
  static constexpr float RawToCommand(int16_t raw) noexcept {
    return static_cast<float>(raw) / kRawFullScale;
  }

 private:
  static constexpr int16_t Saturate(float v) noexcept {
    if (v > 32767.0f) return 32767;
    if (v < -32767.0f) return -32767;
    return static_cast<int16_t>(v);
  }
};

}  // namespace bench
