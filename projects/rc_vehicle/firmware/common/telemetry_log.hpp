#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>

/**
 * @brief Биты маски TelemetryLogFrame::kids_flags
 *
 * Флаги лимитеров Kids Mode: по одному лишь drive_mode нельзя понять, какой
 * именно ограничитель срезал газ в конкретном кадре (LOS-13).
 */
enum KidsFlag : uint8_t {
  kKidsAntiSpinActive = 1u << 0,    // KidsModeProcessor::IsAntiSpinActive()
  kKidsAccelLimitActive = 1u << 1,  // KidsModeProcessor::IsAccelLimitActive()
  kKidsSpeedLimitActive = 1u << 2,  // KidsModeProcessor::IsSpeedLimitActive()
  kKidsLimitersEnabled = 1u << 3,   // kids_mode.limiters_enabled (LOS-286)
};

/**
 * @brief Кадр телеметрии для кольцевого буфера логов
 *
 * Размер: 132 байта (30 × float + uint32_t + 6 × uint8_t + padding).
 * Хранится в PSRAM при наличии (ESP_PLATFORM), иначе в обычной heap.
 *
 * Буфер 52000 кадров × 132 байта ≈ 6.5 МБ; remaining PSRAM is reserved for
 * compact stabilization-configuration deltas.
 */
struct TelemetryLogFrame {
  uint32_t ts_ms{0};          // Метка времени [мс]
  float ax{0}, ay{0}, az{0};  // Ускорение IMU (откалиброванное, в g)
  float gx{0}, gy{0}, gz{0};  // Угловая скорость IMU (dps)
  float vx{0}, vy{0};         // EKF: скорость [м/с]
  float slip_deg{0};          // EKF: угол заноса [градусы]
  float speed_ms{0};          // EKF: полная скорость |v| [м/с]
  float throttle{0};          // Применённый газ (после trim/slew) [-1..1]
  float steering{0};          // Применённый руль (после trim/slew) [-1..1]
  float pitch_deg{0};         // Madgwick: pitch [градусы]
  float roll_deg{0};          // Madgwick: roll [градусы]
  float yaw_deg{0};           // Madgwick: yaw [градусы]
  float yaw_rate_dps{0};      // Отфильтрованный gyro Z [дпс]
  float oversteer_active{0};  // OversteerGuard: 1.0 = занос, 0.0 = нет
  float rc_throttle{0};       // Сырой газ с RC-приёмника [-1..1]
  float rc_steering{0};       // Сырой руль с RC-приёмника [-1..1]
  // --- Новые поля для программы испытаний ---
  float cmd_throttle{0};  // Команда газа до trim/slew [-1..1]
  float cmd_steering{0};  // Команда руля до trim/slew [-1..1]
  float ekf_vx_var{0};    // EKF: дисперсия vx [м²/с²]
  float ekf_vy_var{0};    // EKF: дисперсия vy [м²/с²]
  float ekf_r_var{0};     // EKF: дисперсия yaw rate [рад²/с²]
  float ekf_yaw_deg{0};   // EKF: курсовой угол [°, из магнитометра]
  // --- Магнетометр MMC5983MA ---
  float mx{0};               // Магнитное поле X [мГс]
  float my{0};               // Магнитное поле Y [мГс]
  float mz{0};               // Магнитное поле Z [мГс]
  float heading_deg{0};      // Tilt-compensated magnetic heading [°, 0=N, 90=E]
  float heading_rel_deg{0};  // Относительный курс [°, -180..180]
  uint8_t test_marker{0};    // Маркер теста (0 = нет, >0 = ID теста)
  uint8_t zupt_status{0};    // ZuptStatus на последнем IMU-тике
  uint8_t ekf_diverged{0};   // EKF: 1 = сработал guard расходимости (LOS-233)
  uint8_t drive_mode{0};     // Активный DriveMode (0=Normal..4=DirectLaw)
  uint8_t stab_enabled{0};   // Стабилизация включена (1) / выключена (0)
  uint8_t kids_flags{0};     // Маска активных лимитеров Kids (см. KidsFlag)
  uint8_t _pad[2]{};         // Выравнивание до 4 байт (запас под новые флаги)
};  // sizeof == 132 bytes (30 × float + uint32_t + 6 × uint8_t + 2 pad)

// Compile-time проверка размера структуры
static_assert(sizeof(TelemetryLogFrame) == 132,
              "TelemetryLogFrame size mismatch");

// Веб-парсер лога (esp32_common/web/app.js, FIELD_OFFSETS) читает кадр по
// жёстко зашитым смещениям — сдвиг kids_flags молча испортит выгрузку CSV.
static_assert(offsetof(TelemetryLogFrame, kids_flags) == 129,
              "kids_flags offset must match web/app.js FIELD_OFFSETS");

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
  [[nodiscard]] bool GetFrame(size_t idx, TelemetryLogFrame& out) const;

  /** Freeze the ring for a short external export without duplicating it. */
  [[nodiscard]] bool BeginExport(size_t& count_out);
  [[nodiscard]] bool BeginExport(size_t& count_out,
                                 TelemetryLogFrame& tail_out);
  [[nodiscard]] bool BeginExport(size_t& count_out, TelemetryLogFrame& tail_out,
                                 uint64_t& start_sequence_out);
  [[nodiscard]] uint64_t TotalFramesWritten() const;
  [[nodiscard]] bool GetOldestFrameBoundary(TelemetryLogFrame& frame_out,
                                            uint64_t& sequence_out) const;
  [[nodiscard]] size_t CopyExportFrames(size_t start_idx,
                                        TelemetryLogFrame* out,
                                        size_t max_count) const;
  void EndExport();

  /**
   * @brief Очистить буфер (сбросить счётчики)
   */
  void Clear();

 private:
  TelemetryLogFrame* buf_{nullptr};
  size_t capacity_{0};
  size_t write_pos_{0};
  size_t count_{0};
  uint64_t total_frames_written_{0};
  mutable std::mutex mutex_;
  std::mutex export_mutex_;
  std::unique_lock<std::mutex> export_guard_{};
  size_t export_start_pos_{0};
  size_t export_end_write_pos_{0};
  size_t export_count_{0};
  bool export_active_{false};
};
