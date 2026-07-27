#pragma once

#include "orientation_filter.hpp"

/**
 * Фильтр Madgwick AHRS (IMU, 6DOF) для оценки ориентации по акселерометру и
 * гироскопу. Вход: ax, ay, az (g), gx, gy, gz (град/с). Выход: кватернион и
 * углы Эйлера (pitch, roll, yaw). Без магнитометра — рыскание (yaw) будет
 * дрейфовать; pitch/roll стабильны за счёт акселерометра. Платформонезависимый
 * код (только float-математика).
 *
 * Система координат (по умолчанию — мировая NED):
 * - Кватернион q задаёт поворот из опорной СК в СК датчика (IMU): v_sensor = q
 * ⊗ v_ref ⊗ q*.
 * - По умолчанию опорная СК = NED (X вперёд, Y вправо, Z вниз). После
 * SetVehicleFrame() опорная СК привязана к машине: ось по вектору g (вниз), ось
 * по направлению движения (вперёд), третья — вправо. Тогда при горизонтальной
 * машине pitch = roll = 0.
 * - Углы Эйлера: ZYX. Roll — вокруг X тела, pitch — вокруг Y, yaw — вокруг Z.
 *
 * Смысл yaw после SetVehicleFrame() зависит от наличия магнитометра (LOS-221):
 * - 9DOF: курс АБСОЛЮТНЫЙ — отсчитывается от магнитного поля Земли, как
 *   heading_deg/ekf_yaw_deg. Уже сошедшийся курс переносится в новую СК как
 *   есть; если сохранять нечего (первая калибровка), SetVehicleFrame()
 *   засевает его аналитически из последнего mag-семпла. В обоих случаях
 *   сразу после калибровки yaw ≈ текущему магнитному курсу (НЕ нулю), и
 *   переходного процесса нет.
 * - 6DOF: абсолютной опоры у курса нет, SetVehicleFrame() обнуляет yaw, и
 *   дальше он свободно дрейфует на смещении гироскопа.
 */

struct ImuData;

namespace rc_vehicle {

class MadgwickFilter : public IOrientationFilter {
 public:
  MadgwickFilter();

  // Реализация интерфейса IOrientationFilter
  void Update(float ax, float ay, float az, float gx, float gy, float gz,
              float dt_sec) override;
  void Update(const struct ImuData& imu, float dt_sec) override;

  /**
   * 9DOF MARG-обновление (акселерометр + гироскоп + магнетометр).
   * Стабилизирует yaw по магнитному полю Земли — устраняет дрейф рыскания.
   * Алгоритм: Madgwick MARG, градиентный спуск по объединённой целевой функции.
   */
  void UpdateWithMag(float ax, float ay, float az, float gx, float gy, float gz,
                     float mx, float my, float mz, float dt_sec) override;
  void SetVehicleFrame(const float gravity_vec[3], const float forward_vec[3],
                       bool valid = true) override;
  void GetQuaternion(float& qw, float& qx, float& qy, float& qz) const override;
  void GetEulerRad(float& pitch_rad, float& roll_rad,
                   float& yaw_rad) const override;
  void GetEulerDeg(float& pitch_deg, float& roll_deg,
                   float& yaw_deg) const override;
  void Reset() override;

  // Специфичные для Madgwick методы
  /** Коэффициент коррекции по акселерометру (beta). По умолчанию 0.1; больше —
   * быстрее реакция, больше шум. */
  void SetBeta(float beta) { beta_ = beta; }
  float GetBeta() const { return beta_; }

  /**
   * Адаптивный beta: при линейном ускорении отключает коррекцию по
   * акселерометру (beta → 0), предотвращая ошибки ориентации при разгоне,
   * торможении и поворотах. Срабатывает, когда |a| - 1g| > threshold.
   * @param enabled       Включить адаптивный режим
   * @param threshold_g   Порог отклонения от 1g [g], по умолчанию 0.2
   */
  void SetAdaptiveBeta(bool enabled, float threshold_g = 0.2f) {
    adaptive_enabled_ = enabled;
    adaptive_threshold_g_ = threshold_g;
  }
  [[nodiscard]] bool GetAdaptiveBetaEnabled() const {
    return adaptive_enabled_;
  }
  float GetAdaptiveThresholdG() const { return adaptive_threshold_g_; }

  /**
   * Сбросить накопленную опору курса (yaw_has_absolute_ref_ и прогресс
   * сходимости), не трогая сам кватернион ориентации.
   *
   * Вызывать при смене калибровки магнитометра (MagCalibration::Finish()):
   * mag_calib_->Apply() начинает выдавать другой скорректированный вектор
   * (иной hard-iron offset), и накопленный до этого прогресс относился к
   * СТАРОЙ калибровке (или вовсе к сырым, некалиброванным данным) — курс,
   * посчитанный по нему, ещё не сошёлся под НОВУЮ калибровку. Без сброса
   * IMU-калибровка, завершившаяся вскоре после смены mag-калибровки, могла
   * бы закрепить курс, посчитанный по устаревшей магнитной опоре (review
   * r3630102909, LOS-229).
   */
  void InvalidateYawTrust() {
    yaw_has_absolute_ref_ = false;
    marg_correction_progress_ = 0.f;
    // Кэш засева относится к СТАРОЙ mag-калибровке — засевать по нему новый
    // vehicle frame нельзя ровно по той же причине, что и сохранять курс
    // (LOS-221). Следующий UpdateWithMag() наполнит кэш заново.
    last_mag_valid_ = false;
  }

 private:
  float q0_{1.f}, q1_{0.f}, q2_{0.f}, q3_{0.f};
  float beta_{0.1f};

  // Адаптивный beta: отключение коррекции при линейном ускорении
  bool adaptive_enabled_{false};
  float adaptive_threshold_g_{0.2f};

  // Есть ли у курса абсолютная опора: true после серии 9DOF-обновлений с
  // магнитометром, false после 6DOF (там yaw — только дрейф гироскопа).
  // От этого зависит, сохранять ли курс в SetVehicleFrame().
  bool yaw_has_absolute_ref_{false};

  // Опора считается абсолютной только после накопления MARG-коррекции,
  // взвешенной по beta, — не тиков и не голого времени. Продакшен вызывает
  // UpdateWithMag каждые 2 мс (500 Гц control loop) независимо от частоты
  // обновления самого магнитометра (ImuHandler::FeedMadgwick), поэтому
  // счётчик «подряд идущих обновлений» открывался за ~100 мс, хотя
  // градиентный спуск при дефолтном beta=0.1 реально сходится за ~10-11 с
  // (см. телеметрию LOS-229, review r3628818049). Скорость сходимости
  // пропорциональна beta, а конфиг допускает madgwick_beta от 0.01 до 1.0
  // (FilterConfig::Clamp, stabilization_config.cpp) и может меняться на
  // ходу через StabilizationManager::ApplyToFilters — поэтому копим
  // effective_beta * dt_sec (вклад тика в реальную сходимость), а не сырое
  // время: секунды, накопленные при низком beta, не переоцениваются, если
  // beta потом увеличили (review r3629340617, LOS-229). Тики с
  // effective_beta == 0 (адаптивный beta при разгоне/торможении) вклада не
  // дают. Любой провал в 6DOF обнуляет накопитель.
  //
  // Порог — калибровочная точка: при постоянном beta=kReferenceBeta (0.1)
  // накопитель достигает её за kReferenceSecondsForYawRef (12 с, с запасом
  // над наблюдаемыми ~10-11 с).
  float marg_correction_progress_{0.f};
  static constexpr float kReferenceBeta = 0.1f;
  static constexpr float kReferenceSecondsForYawRef = 12.0f;
  static constexpr float kMinMargProgressForYawRef =
      kReferenceBeta * kReferenceSecondsForYawRef;

  // Последний mag-вектор, поданный в UpdateWithMag() (СК датчика, после
  // hard-iron коррекции — ровно тот, что видит градиентный спуск).
  //
  // LOS-221: SetVehicleFrame() раньше выставлял ψ=0, когда курс не был
  // подкреплён магнитометром, и MARG потом десятками секунд вытягивал курс
  // градиентным спуском к магнитному северу — на стоящей машине это выглядело
  // как дрейф yaw при gz≈0 (телеметрия: 0°→61° за 13 с, boot-лог LOS-213:
  // 23.7°→122.8° за ~110 с). На загрузочной калибровке гейт
  // kMinMargProgressForYawRef не открывается в принципе: Full-калибровка (1000
  // семплов) завершается за 2-4 с, а порогу нужно 12 с при beta=0.1.
  //
  // Теперь ψ вычисляется аналитически из этого вектора сразу в точку
  // равновесия MARG (см. SetVehicleFrame), поэтому догонять нечего.
  // Держим вектор здесь, а не параметром SetVehicleFrame(): так засев
  // структурно берёт ИМЕННО то поле, по которому сходится фильтр, и все
  // вызывающие (CalibrationManager, ImuHandler) получают его без новой
  // зависимости от магнитометра.
  float last_mag_x_{0.f}, last_mag_y_{0.f}, last_mag_z_{0.f};
  bool last_mag_valid_{false};

  // Минимальная доля горизонтальной проекции mag в СК машины (по квадрату)
  // от полного модуля поля. Поле почти вертикально → курс из него не
  // восстановить, уходим в фолбэк.
  //
  // Порог именно ОТНОСИТЕЛЬНЫЙ: размерность mag по контракту
  // IOrientationFilter::UpdateWithMag произвольна («нужна только нормировка»),
  // и абсолютная константа сделала бы результат зависящим от масштаба — одно
  // и то же поле в разных единицах уходило бы по разным веткам (ревью PR #308).
  // 1e-8 по квадрату = горизонталь ≥ 1e-4 от |m|: на порядки ниже шума любого
  // реального датчика, поэтому отсекается только истинное вырождение.
  static constexpr float kMinHorizFractionSq = 1e-8f;

  // Опорная СК машины: q_veh_to_ned (поворот из СК машины в NED), только если
  // use_vehicle_frame_
  bool use_vehicle_frame_{false};
  float q_veh_to_ned_0_{1.f}, q_veh_to_ned_1_{0.f}, q_veh_to_ned_2_{0.f},
      q_veh_to_ned_3_{0.f};

  /**
   * Вычислить курс, в котором MARG находится в равновесии, по последнему
   * mag-семплу и осям только что построенной СК машины (LOS-221).
   *
   * @param x_veh_* — ось «вперёд» СК машины в СК датчика
   * @param y_veh_* — ось «влево» СК машины в СК датчика
   * @param yaw_rad — [out] курс в радианах, заполняется только при успехе
   * @return false, если свежего mag нет либо горизонтальная проекция поля
   *         вырождена — тогда вызывающий уходит в прежний фолбэк
   */
  bool SeedYawFromMag(float x_veh_x, float x_veh_y, float x_veh_z,
                      float y_veh_x, float y_veh_y, float y_veh_z,
                      float& yaw_rad) const;

  void GetQuaternionInNed(float& qw, float& qx, float& qy, float& qz) const;
  static void QuatMul(float aw, float ax, float ay, float az, float bw,
                      float bx, float by, float bz, float& ow, float& ox,
                      float& oy, float& oz);
  static float InvSqrt(float x);
};

}  // namespace rc_vehicle
