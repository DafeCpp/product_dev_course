#pragma once

#include <cstdint>

/** Данные магнитометра: поле в мГс (milli-Gauss). */
struct MagData {
  float mx{0.f}, my{0.f}, mz{0.f};
};

/**
 * Абстрактный интерфейс магнитометра.
 * Реализован Mmc5983Spi.
 */
class IMagSensor {
 public:
  virtual ~IMagSensor() = default;

  /** Инициализация датчика. 0 — успех, -1 — ошибка. */
  virtual int Init() = 0;

  /** Чтение данных. 0 — успех, -1 — ошибка. */
  virtual int Read(MagData& data) = 0;

  /** Последнее значение Product ID из регистра 0x2F (-1 = не читали). */
  virtual int GetLastProductId() const = 0;
};
