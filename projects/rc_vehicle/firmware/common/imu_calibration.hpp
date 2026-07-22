#pragma once

#include <cstdint>

#include "mpu6050_spi.hpp"  // ImuData

namespace rc_vehicle {

/** Калибровочные данные IMU: bias, вектор g при покое, направление «вперёд». */
struct ImuCalibData {
  float gyro_bias[3]{0.f, 0.f, 0.f};   // gx, gy, gz offset (dps)
  float accel_bias[3]{0.f, 0.f, 0.f};  // ax, ay, az offset (g)
  /** Единичный вектор g в СК датчика (этап 1: стояние на месте). */
  float gravity_vec[3]{0.f, 0.f, 1.f};
  /** Единичный вектор «вперёд» в СК датчика (этап 2: движение вперёд/назад).
   * Продольное ускорение = dot(accel − gravity_vec, vec). */
  float accel_forward_vec[3]{1.f, 0.f, 0.f};
  /** accel_forward_vec получен реальной Forward-калибровкой и прошёл проверку
   * на горизонтальность. false → используется дефолтная ось X. Не хранится в
   * NVS: пересчитывается при загрузке в SetData(). */
  bool forward_valid{false};
  /** Смещение IMU от центра масс [м]: (rx, ry) в СК датчика.
   * Определяется круговой калибровкой (CW+CCW). */
  float com_offset[2]{0.f, 0.f};
  bool valid{false};
};

/** Режим калибровки. */
enum class CalibMode {
  GyroOnly,  // только гироскоп (быстро, ~2 сек)
  Full,      // этап 1: стояние на месте — gyro/accel bias + вектор g
  Forward,  // этап 2: движение вперёд/назад с прямыми колёсами — вектор
            // «вперёд»
};

/** Состояние процесса калибровки. */
enum class CalibStatus {
  Idle,        // калибровка не запущена
  Collecting,  // идёт сбор семплов
  Done,        // калибровка завершена успешно
  Failed,      // калибровка не удалась (движение обнаружено)
};

/**
 * Калибровка IMU: сбор bias гироскопа/акселерометра, применение компенсации.
 *
 * Использование:
 *   1. (опционально) SetData() — загрузить сохранённые данные из NVS
 *   2. StartCalibration(mode) — запустить авто-калибровку
 *   3. В control loop: FeedSample(raw) на каждом семпле (500 Гц)
 *   4. Когда GetStatus() == Done — калибровка завершена
 *   5. Apply(data) — вычесть bias из сырых данных перед обработкой
 *
 * Платформонезависимый: не зависит от ESP-IDF, FreeRTOS и т.д.
 */
class ImuCalibration {
 public:
  /** Запустить этап 1 (Full) или GyroOnly. num_samples — количество семплов. */
  void StartCalibration(CalibMode mode, int num_samples = 1000);

  /** Запустить этап 2 (Forward): требует валидную калибровку с gravity_vec.
   * num_samples — сбор при движении вперёд/назад. */
  bool StartForwardCalibration(int num_samples = 2000);

  /** Подать очередной семпл (вызывать каждую итерацию control loop при
   * Collecting). */
  void FeedSample(const ImuData& raw);

  /** Текущий этап калибровки: 0 = нет, 1 = стояние на месте, 2 = движение
   * вперёд/назад. */
  int GetCalibStage() const;

  /** Применить компенсацию bias к данным (вычитание). */
  void Apply(ImuData& data) const;

  /**
   * Продольное ЛИНЕЙНОЕ ускорение (вперёд/назад) в g.
   * Вызывать после Apply(data). Положительное = ускорение вперёд.
   *
   * Считается как скалярное произведение (accel − gravity_vec) на единичный
   * вектор направления: гравитацию обязательно вычитаем, иначе остаточный
   * наклон оси «вперёд» даёт постоянный офсет (LOS-214). В покое на ровной
   * площадке возвращает ~0.
   */
  float GetForwardAccel(const ImuData& data) const;

  /** Задать направление «вперёд» единичным вектором в СК датчика (fx,fy,fz).
   * Нормализуется и приводится к горизонтали (⊥ gravity_vec). */
  void SetForwardDirection(float fx, float fy, float fz);

  /**
   * Повернуть акселерометр и гироскоп из СК датчика в СК машины.
   *
   * Базис СК машины: Z — gravity_vec (вверх), X — accel_forward_vec (вперёд),
   * Y = Z×X (влево — согласовано с конвенцией VehicleEkf: vy>0/yaw rate>0 =
   * «влево», проверено тестом RotateToVehicleFrame_YAxisMatchesEkfLeft
   * PositiveConvention) — те же оси, что строит MadgwickFilter::
   * SetVehicleFrame() для вывода Euler-углов. Bias-коррекция (Apply())
   * только сдвигает начало отсчёта и не поворачивает оси, поэтому при
   * наклонном монтаже bias-corrected ax/ay/gx/gy остаются смесью осей
   * датчика — источники тангажа, не прошедшие эту ротацию (в отличие от
   * Madgwick), дают систематическую ошибку на наклонном монтаже.
   *
   * Вызывать ПОСЛЕ Apply(). При отсутствии калибровки (дефолтные
   * gravity_vec=(0,0,1), accel_forward_vec=(1,0,0)) — тождественное
   * преобразование.
   */
  void RotateToVehicleFrame(ImuData& data) const;

  /** Текущий статус калибровки. */
  CalibStatus GetStatus() const { return status_; }

  /** Получить текущие калибровочные данные. */
  const ImuCalibData& GetData() const { return data_; }

  /** Загрузить калибровочные данные (из NVS или внешнего источника). */
  void SetData(const ImuCalibData& data);

  /**
   * Прервать идущий сбор семплов (Collecting → Failed).
   *
   * Нужно при досрочной остановке авто-движения: иначе сбор продолжится уже
   * без управляемого разгона и завершится записью мусорной оси «вперёд».
   * No-op, если сбор не идёт — не затирает Done.
   */
  void CancelCalibration();

  /**
   * Коррекция акселерометра за смещение IMU от центра масс.
   *
   * Вычитает центростремительную и тангенциальную составляющие,
   * вызванные offset (rx, ry) между IMU и CoM.
   *
   * Формулы (в g):
   *   ax_corrected = ax + (ω²·rx + α·ry) / g
   *   ay_corrected = ay + (ω²·ry − α·rx) / g
   *
   * @param data  Откалиброванные IMU-данные (после Apply). Модифицируется in-place.
   * @param omega_rad_s  Угловая скорость рыскания [рад/с]
   * @param alpha_rad_s2 Угловое ускорение рыскания [рад/с²]
   */
  void CorrectForComOffset(ImuData& data, float omega_rad_s,
                           float alpha_rad_s2) const;

  /** Калибровка валидна и можно применять Apply(). */
  [[nodiscard]] bool IsValid() const { return data_.valid; }

  // Пороги для детекции движения (variance по оси)
  static constexpr float kGyroVarianceThreshold = 0.5f;    // (dps)^2
  static constexpr float kAccelVarianceThreshold = 0.01f;  // (g)^2

  // Максимально допустимый bias (для валидации данных из NVS)
  static constexpr float kMaxGyroBias = 20.0f;  // dps
  static constexpr float kMaxAccelBias = 0.5f;  // g

  // Максимальный |forward · gravity| для наземной машины. Ось «вперёд»
  // горизонтальна, поэтому заметная проекция на гравитацию означает
  // испорченную калибровку (LOS-214).
  static constexpr float kMaxForwardTilt = 0.5f;

 private:
  ImuCalibData data_{};
  CalibStatus status_{CalibStatus::Idle};
  CalibMode mode_{CalibMode::GyroOnly};

  // Аккумуляторы этап 1 (Welford)
  int target_samples_{0};
  int collected_{0};
  double sum_[6]{};
  double sum_sq_[6]{};

  // Аккумуляторы этап 2 (линейное ускорение при движении)
  double sum_linear_[3]{};
  float first_linear_[3]{};
  bool first_linear_set_{false};

  static constexpr float kLinearAccelThreshold =
      0.05f;  // (g) порог для учёта семпла

  /**
   * Показание акселерометра в ПОКОЕ после Apply().
   *
   * Accel bias поглощает компоненты наклона (ax, ay в покое), поэтому
   * bias-corrected покой нормализуется в (0,0,±1) — см. control_components.cpp.
   * Вычитание этого вектора даёт динамическую (линейную) часть ускорения.
   *
   * @note Это НЕ замена gravity_vec как направления гравитации: Apply()
   *       сдвигает начало отсчёта, но не поворачивает СК. Для ориентации осей
   *       (ортогонализация «вперёд») опорой остаётся сырой gravity_vec.
   */
  void RestDownVec(float* out) const;

  void ResetAccumulators();
  bool Finalize();
  bool FinalizeForward();
};

}  // namespace rc_vehicle
