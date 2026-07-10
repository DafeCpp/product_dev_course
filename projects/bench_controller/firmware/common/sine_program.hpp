#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace bench {

/**
 * @brief Программа нагружения из синусоидальных сегментов
 *
 * Минимум для спайка: последовательность блоков {среднее, амплитуда,
 * частота, число циклов} (block loading). Фаза непрерывна внутри
 * сегмента; переход к следующему — по завершении целого числа циклов
 * (фаза кратна 2π), поэтому стыки сегментов без скачка значения
 * возможны при совпадении mean. После последнего сегмента программа
 * удерживает его среднее значение (Finished() == true).
 *
 * Стейт-машина продвигается вызовом Step(dt) из тика контура —
 * привязки к wall-clock нет, что упрощает SIL с логическим временем.
 */
class SineProgram {
 public:
  struct Segment {
    float mean{0.0f};       ///< Среднее значение (Н или мм)
    float amplitude{0.0f};  ///< Амплитуда
    float freq_hz{1.0f};    ///< Частота процесса
    uint32_t cycles{1};     ///< Число циклов сегмента
  };

  SineProgram() = default;
  explicit SineProgram(std::span<const Segment> segments);

  /// Продвинуть программу на dt и получить целевое значение
  [[nodiscard]] float Step(float dt_sec) noexcept;

  /// Среднее текущего сегмента (центр разгрузки при ramp-down)
  [[nodiscard]] float CurrentMean() const noexcept;

  /// Все сегменты завершены — программа держит финальное среднее
  [[nodiscard]] bool Finished() const noexcept { return finished_; }

  /// Полных циклов, отработанных в текущем сегменте
  [[nodiscard]] uint32_t CyclesDone() const noexcept { return cycles_done_; }

  /// Индекс текущего сегмента
  [[nodiscard]] size_t SegmentIndex() const noexcept { return index_; }

 private:
  static constexpr size_t kMaxSegments = 8;

  Segment segments_[kMaxSegments]{};
  size_t count_{0};
  size_t index_{0};
  float phase_{0.0f};  ///< Радианы в пределах текущего цикла [0..2π)
  uint32_t cycles_done_{0};
  bool finished_{true};
};

}  // namespace bench
