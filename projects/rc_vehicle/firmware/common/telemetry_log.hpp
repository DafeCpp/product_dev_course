#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>

/**
 * @brief Кадр телеметрии для кольцевого буфера логов
 *
 * Размер: 116 байт (27 × float + uint32_t + uint8_t + padding).
 * Хранится в PSRAM при наличии (ESP_PLATFORM), иначе в обычной heap.
 *
 * Буфер 60000 кадров × 116 байт ≈ 6.6 МБ (PSRAM из 16 МБ).
 */
struct TelemetryLogFrame {
  uint32_t ts_ms{0};           // Метка времени [мс]
  float ax{0}, ay{0}, az{0};   // Ускорение IMU (откалиброванное, в g)
  float gx{0}, gy{0}, gz{0};   // Угловая скорость IMU (dps)
  float vx{0}, vy{0};          // EKF: скорость [м/с]
  float slip_deg{0};            // EKF: угол заноса [градусы]
  float speed_ms{0};            // EKF: полная скорость |v| [м/с]
  float throttle{0};            // Применённый газ (после trim/slew) [-1..1]
  float steering{0};            // Применённый руль (после trim/slew) [-1..1]
  float pitch_deg{0};           // Madgwick: pitch [градусы]
  float roll_deg{0};            // Madgwick: roll [градусы]
  float yaw_deg{0};             // Madgwick: yaw [градусы]
  float yaw_rate_dps{0};        // Отфильтрованный gyro Z [дпс]
  float oversteer_active{0};    // OversteerGuard: 1.0 = занос, 0.0 = нет
  float rc_throttle{0};         // Сырой газ с RC-приёмника [-1..1]
  float rc_steering{0};         // Сырой руль с RC-приёмника [-1..1]
  // --- Новые поля для программы испытаний ---
  float cmd_throttle{0};        // Команда газа до trim/slew [-1..1]
  float cmd_steering{0};        // Команда руля до trim/slew [-1..1]
  float ekf_vx_var{0};          // EKF: дисперсия vx [м²/с²]
  float ekf_vy_var{0};          // EKF: дисперсия vy [м²/с²]
  float ekf_r_var{0};           // EKF: дисперсия yaw rate [рад²/с²]
  // --- Магнетометр MMC5983MA ---
  float mx{0};                   // Магнитное поле X [мГс]
  float my{0};                   // Магнитное поле Y [мГс]
  float mz{0};                   // Магнитное поле Z [мГс]
  float heading_deg{0};          // Tilt-compensated magnetic heading [°, 0=N, 90=E]
  uint8_t test_marker{0};       // Маркер теста (0 = нет, >0 = ID теста)
  uint8_t _pad[3]{};            // Выравнивание до 4 байт
};  // sizeof == 120 bytes (28 × float + uint32_t + uint8_t + 3 pad)

// Compile-time проверка размера структуры
static_assert(sizeof(TelemetryLogFrame) == 120,
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
