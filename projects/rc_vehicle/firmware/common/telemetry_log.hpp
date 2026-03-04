#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>

/**
 * @brief Кадр телеметрии для кольцевого буфера логов
 *
 * Размер: 52 байта (12 × float4 + uint32_t).
 * Хранится в PSRAM при наличии (ESP_PLATFORM), иначе в обычной heap.
 */
struct TelemetryLogFrame {
  uint32_t ts_ms{0};      // Метка времени [мс]
  float ax{0}, ay{0}, az{0};  // Ускорение IMU (откалиброванное, в g)
  float gx{0}, gy{0}, gz{0};  // Угловая скорость IMU (dps)
  float vx{0}, vy{0};         // EKF: скорость [м/с]
  float slip_deg{0};           // EKF: угол заноса [градусы]
  float speed_ms{0};           // EKF: полная скорость |v| [м/с]
  float throttle{0};           // Команда газа [-1..1]
  float steering{0};           // Команда руля [-1..1]
};  // sizeof == 52 bytes (uint32_t + 12 × float)

// Compile-time проверка размера структуры
static_assert(sizeof(TelemetryLogFrame) == 52,
              "TelemetryLogFrame size mismatch");

/**
 * @brief Потокобезопасный кольцевой буфер кадров телеметрии
 *
 * Буфер выделяется в PSRAM (если доступен), иначе в обычной heap.
 * Push() вытесняет старые данные при переполнении.
 * Чтение через GetFrame(idx=0) → oldest, idx=Count()-1 → newest.
 *
 * @note Не копируется и не перемещается.
 */
class TelemetryLog {
 public:
  TelemetryLog() = default;
  ~TelemetryLog();

  TelemetryLog(const TelemetryLog&) = delete;
  TelemetryLog& operator=(const TelemetryLog&) = delete;

  /**
   * @brief Инициализировать буфер
   * @param capacity_frames Максимальное количество кадров
   * @return true при успешном выделении памяти
   */
  bool Init(size_t capacity_frames);

  /**
   * @brief Записать кадр в буфер (вытесняет старые при переполнении)
   * @param frame Кадр телеметрии
   */
  void Push(const TelemetryLogFrame& frame);

  /**
   * @brief Текущее количество сохранённых кадров
   */
  [[nodiscard]] size_t Count() const;

  /**
   * @brief Ёмкость буфера (количество кадров)
   */
  [[nodiscard]] size_t Capacity() const { return capacity_; }

  /**
   * @brief Получить кадр по индексу (0 = oldest, Count()-1 = newest)
   * @param idx Индекс кадра
   * @param out Выходной кадр
   * @return true если idx < Count()
   */
  bool GetFrame(size_t idx, TelemetryLogFrame& out) const;

  /**
   * @brief Очистить буфер (сбросить счётчики)
   */
  void Clear();

 private:
  TelemetryLogFrame* buf_{nullptr};
  size_t capacity_{0};
  size_t write_pos_{0};
  size_t count_{0};
  mutable std::mutex mutex_;
};
