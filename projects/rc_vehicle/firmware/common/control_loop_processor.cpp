#include "control_loop_processor.hpp"

#include <cmath>

#include "config.hpp"
#include "diagnostics_reporter.hpp"
#include "drive_mode_registry.hpp"
#include "telemetry_builder.hpp"

#ifdef ESP_PLATFORM
#include "udp_telem_sender.hpp"
#endif

#ifdef RC_PROFILE_LOOP
#include "log_format.hpp"
// FW-R16: засечки времени по стадиям итерации. PROF_START() заводит локальный
// курсор времени, PROF_LAP(acc) добавляет дельту с прошлой засечки в
// аккумулятор и сдвигает курсор. В обычной сборке — пустышки (нулевой оверхед,
// без _pt).
#define PROF_START()                        \
  uint64_t _pt = ctx_.platform.GetTimeUs(); \
  uint64_t _prof_iter_us = 0
#define PROF_LAP(acc, max_acc)                     \
  do {                                             \
    const uint64_t _n = ctx_.platform.GetTimeUs(); \
    const uint64_t _d = _n - _pt;                  \
    (acc) += _d;                                   \
    if (_d > (max_acc)) (max_acc) = _d;            \
    _prof_iter_us += _d;                           \
    _pt = _n;                                      \
  } while (0)
// Не делает нового замера времени — переиспользует дельты, накопленные
// в PROF_LAP() за эту итерацию (LOS-219). Outlier считается по dt_ms
// (реальный период между вызовами Step(), см. ControlTaskLoop), а не по
// _prof_iter_us (время ТОЛЬКО внутри Step()) — код-ревью PR #297: stall,
// произошедший пока таск заблокирован в DelayUntilNextTick() (напр.
// зависание flash-cache от NVS commit на другом ядре), не тронул бы
// _prof_iter_us, и outliers остался бы 0 при реально пропущенном такте.
#define PROF_END(dt_ms)                                                       \
  do {                                                                        \
    if (_prof_iter_us > prof_step_max_us_) prof_step_max_us_ = _prof_iter_us; \
    const uint64_t _period_us = static_cast<uint64_t>(dt_ms) * 1000;          \
    if (_period_us > prof_period_max_us_) prof_period_max_us_ = _period_us;   \
    if (_period_us > config::ProfilingConfig::kOutlierThresholdUs)            \
      ++prof_outliers_;                                                       \
  } while (0)
#else
#define PROF_START() ((void)0)
#define PROF_LAP(acc, max_acc) ((void)0)
#define PROF_END(dt_ms) ((void)0)
#endif

namespace rc_vehicle {

void ControlLoopProcessor::Step(uint32_t now, uint32_t dt_ms) {
  ++diag_loop_count_;

  // Единственный snapshot конфига на итерацию (FW-RF5): одна копия под
  // мьютексом вместо трёх (Step/UpdateWeights/диагностика) на 500 Гц.
  stab_cfg_ =
      ctx_.stab_mgr ? ctx_.stab_mgr->GetConfig() : StabilizationConfig{};

  PROF_START();

  UpdateComponents(now, dt_ms);  // RC/WiFi/IMU read + Madgwick + LPF
  PROF_LAP(prof_components_us_, prof_components_max_us_);
  UpdateSensorsAndEkf(dt_ms);  // snapshot + ComOffset + EKF
  PROF_LAP(prof_sensors_us_, prof_sensors_max_us_);

  if (ctx_.calib_mgr) {
    ctx_.calib_mgr->ProcessRequest(now);
    ctx_.calib_mgr->ProcessCompletion(now);
    // Отложенный SetForwardDirection() (WS-команда, код-ревью PR #290,
    // 7-й раунд) — применяется здесь же, на потоке control loop, где
    // единственно безопасно трогать imu_calib_/madgwick_.
    ctx_.calib_mgr->ProcessForwardDirectionRequest();
    // Завершение Full/Forward калибровки меняет базис RotateToVehicleFrame()
    // (код-ревью PR #290, 5-й раунд): tilt_est_ уже мог сойтись под ПРЕЖНИМ
    // базисом — без сброса эти тангаж/крен интерпретировались бы в НОВОЙ СК
    // как есть, до нескольких секунд ложной grav-компенсации (corr_gain_hz
    // по умолчанию 0.5 — медленно). prev_vx_/a_lin_prev_g_ тоже сбрасываем:
    // EKF только что обнулён (ProcessCompletion() выше), и без сброса
    // конечная разность на следующем тике дала бы фиктивный скачок против
    // «протухшего» prev_vx_ (см. комментарий у a_lin_prev_g_ в .hpp).
    if (ctx_.calib_mgr->ConsumeFrameChanged()) {
      tilt_est_.Reset();
      prev_vx_ = ctx_.ekf.GetVx();
      a_lin_prev_g_ = 0.0f;
    }
  }

  SelectControlSource(sensors_, commanded_throttle_, commanded_steering_);
  UpdateAutoDrive(now, dt_ms);
  PROF_LAP(prof_control_us_, prof_control_max_us_);

  UpdateStabilization(dt_ms);
  PROF_LAP(prof_stab_us_, prof_stab_max_us_);
  // При активном failsafe UpdatePwm пропускается: иначе SetPwm(0 + trim)
  // перезаписал бы нейтраль ненулевым trim'ом — моторы ползли бы при
  // потере сигнала (FW-R1).
  if (!HandleFailsafe()) {
    UpdatePwm(now, dt_ms);
  }
  PROF_LAP(prof_pwm_us_, prof_pwm_max_us_);
  UpdateTelemetry(now, dt_ms);
  PROF_LAP(prof_telem_us_, prof_telem_max_us_);

  {
    const DiagnosticsContext dctx{ctx_.platform,    *ctx_.stab_mgr,
                                  ctx_.madgwick,    ctx_.ekf,
                                  ctx_.imu_handler, ctx_.last_loop_hz};
#ifdef RC_PROFILE_LOOP
    const uint32_t prof_loops = diag_loop_count_;
#endif
    PrintDiagnostics(dctx, stab_cfg_, now, diag_loop_count_, diag_start_ms_);
#ifdef RC_PROFILE_LOOP
    // diag_loop_count_ обнуляется в PrintDiagnostics, когда сработал интервал —
    // это и есть сигнал напечатать средние и сбросить аккумуляторы.
    if (diag_loop_count_ == 0) EmitProfile(prof_loops);
#endif
  }
  // PROF_LAP для diag-стадии и PROF_END() — ПОСЛЕ диагностики (код-ревью
  // PR #297): PrintDiagnostics()/EmitProfile() сами делают Log()-вызовы,
  // которые могут быть небыстрыми (UART на низком baud) — раз в диаг-
  // интервал, но предсказуемо. Раньше эта работа не входила ни в один
  // per-stage max, а PROF_END() уже отработал — step_iter не видел эту
  // стадию вообще, хотя в СЛЕДУЮЩЕЙ итерации dt_ms её всё равно захватил
  // бы (now-last_loop считается ДО следующего Step()), создавая ложное
  // расхождение period/step_iter, будто stall произошёл ВНЕ Step().
  PROF_LAP(prof_diag_us_, prof_diag_max_us_);
  PROF_END(dt_ms);
}

void ControlLoopProcessor::UpdateComponents(uint32_t now, uint32_t dt_ms) {
  if (ctx_.rc_handler) ctx_.rc_handler->Update(now, dt_ms);
  if (ctx_.wifi_handler) ctx_.wifi_handler->Update(now, dt_ms);
  if (ctx_.imu_handler) ctx_.imu_handler->Update(now, dt_ms);
}

void ControlLoopProcessor::UpdateSensorsAndEkf(uint32_t dt_ms) {
  sensors_ =
      BuildSensorSnapshot(ctx_.rc_handler, ctx_.wifi_handler, ctx_.imu_handler);
  prev_gz_rad_s_ =
      CorrectImuForComOffset(sensors_, ctx_.imu_calib, prev_gz_rad_s_, dt_ms);

  const bool ekf_active = ctx_.stab_mgr && stab_cfg_.filter.ekf_enabled;
  // tilt_was_enabled_ отслеживает, вызывался ли tilt_est_.Update() на
  // ПРЕДЫДУЩЕМ тике — установка отложена до конца функции (безусловно,
  // независимо от того, войдём ли вообще в блок ниже), чтобы ловить ЛЮБОЙ
  // путь, из-за которого Update() пропускается: не только
  // tilt_comp_enabled=false (9-й раунд), но и ekf_enabled=false,
  // imu_enabled=false, dt_ms==0 (код-ревью PR #290, 10-й раунд) — во всех
  // случаях pitch_rad_/roll_rad_ замораживаются одинаково.
  bool tilt_active_this_tick = false;
  if (ekf_active && sensors_.imu_enabled && dt_ms > 0) {
    const float dt_sec = static_cast<float>(dt_ms) * 0.001f;
    constexpr float kG = 9.80665f;

    // Ротация в СК машины (код-ревью PR #290, 3-й раунд): и EKF (grav_x/
    // grav_y от pitch_rad/roll_rad — СК машины), и TiltEstimator ожидают
    // vehicle-frame accel/gyro, а sensors_.imu_data — bias-corrected, но НЕ
    // повёрнутые данные в СК ДАТЧИКА. При наклонном и/или yaw-смещённом
    // монтаже (Forward-калибровка существует именно для произвольного
    // разворота IMU на плате) без поворота реальное продольное ускорение
    // могло бы частично или полностью уйти в «боковую» ось EKF — machine
    // считала бы, что не разгоняется, а сносит вбок. gz НЕ поворачиваем:
    // sensors_.filtered_gz — общий LPF-сигнал yaw rate для yaw-rate control/
    // auto-drive/калибровок (stabilization_pipeline.cpp,
    // control_loop_helpers.hpp), и его поворот только для EKF завёл бы два
    // рассинхронизированных «yaw rate» в системе; для чистого yaw-монтажа gz
    // инвариантен (вращение вокруг Z не меняет Z-компоненту), полный фикс —
    // перенос ротации перед LPF для всех потребителей разом, отдельная
    // задача.
    ImuData veh_imu = sensors_.imu_data;
    ctx_.imu_calib.RotateToVehicleFrame(veh_imu);

    // Мотор-модельный якорь (LOS-233) гейтится этим флагом — единственный
    // сигнал в системе, действительно независимый от IMU/EKF/тангажа.
    // a_lin_g (ниже) использует a_lin_prev_g_ ТОЛЬКО пока якорь активен —
    // без него EKF vx не является независимым источником (см. комментарий
    // у a_lin_prev_g_ в .hpp).
    const auto& f = stab_cfg_.filter;
    const bool motor_model_active =
        f.motor_model_enabled && !ctx_.auto_drive.IsSpeedCalibActive();

    // Ориентация для снятия проекции гравитации из ускорения перед
    // интеграцией в EKF. Источник по умолчанию — TiltEstimator (LOS-240):
    // комплементарный фильтр, не загрязняемый линейным ускорением (в
    // отличие от Madgwick — см. tilt_estimator.hpp). При выключенном
    // tilt-фильтре — фолбэк на Madgwick (обратная совместимость); при
    // выключенных обоих — 0 (без grav-компенсации).
    float pitch_rad = 0.0f, roll_rad = 0.0f;
    if (stab_cfg_.filter.tilt_comp_enabled) {
      tilt_active_this_tick = true;
      if (!tilt_was_enabled_) {
        // Возобновление после ЛЮБОГО перерыва (см. комментарий у
        // tilt_active_this_tick выше): pitch_rad_/roll_rad_ заморожены с
        // последнего Update() — сбрасываем, иначе EKF получит протухший
        // тангаж/крен из интервала простоя.
        tilt_est_.Reset();
      }
      const float a_lin_g = motor_model_active ? a_lin_prev_g_ : 0.0f;
      // Боковое (центростремительное) ускорение для roll-коррекции —
      // симметричный аналог a_lin_g для pitch (код-ревью PR #290, 6-й
      // раунд): без него устойчивый разворот с боковым ускорением ~0.2g
      // заваливал бы roll тем же путём, каким продольный разгон заваливал
      // pitch без a_lin_g. a = ω×v для тела, вращающегося вокруг Z со
      // скоростью gz и движущегося вперёд с vx: a_y = gz·vx (Y_veh,
      // veh_imu.gz — уже в СК машины, ROTATED выше). В отличие от a_lin_g
      // НЕ гейтится якорем: vx влияет через pitch/grav_x, roll в этой
      // формуле не участвует вовсе — циркулярности для roll нет ни при
      // каком источнике vx (в худшем случае — унаследованная неточность
      // vx без якоря, не новая расходимость).
      constexpr float kDegToRad = 3.14159265358979f / 180.0f;
      const float a_lin_lat_g = (veh_imu.gz * kDegToRad) * prev_vx_ / kG;
      tilt_est_.SetParams({stab_cfg_.filter.tilt_corr_gain_hz,
                           stab_cfg_.filter.tilt_accel_gate_band_g});
      tilt_est_.Update(veh_imu, a_lin_g, a_lin_lat_g, dt_sec);
      pitch_rad = tilt_est_.GetPitchRad();
      roll_rad = tilt_est_.GetRollRad();
    } else {
      if (stab_cfg_.filter.madgwick_enabled) {
        float yaw_rad = 0.0f;
        ctx_.madgwick.GetEulerRad(pitch_rad, roll_rad, yaw_rad);
      }
    }
    // Передаём |commanded_throttle_| для ZUPT gating:
    // если throttle > 2%, ZUPT не применяется (машина пытается ехать).
    ctx_.ekf.UpdateFromImu(veh_imu.ax, veh_imu.ay, veh_imu.az,
                           sensors_.filtered_gz, dt_sec,
                           std::abs(commanded_throttle_), pitch_rad, roll_rad);

    // Якорь продольной скорости через мотор-модель (LOS-233): без датчика
    // колёс единственный способ не дать vx уйти в разнос при интеграции IMU.
    // v ≈ gain·throttle (с мёртвой зоной) подаётся слабым измерением.
    // Вход модели — applied_throttle_ (значение прошлого тика, после slew и
    // trim): это то, что реально ушло в PWM. Команда при slew-рампе прыгает
    // мгновенно и завышала бы ожидаемую скорость на всё время рампы.
    if (motor_model_active) {
      const float thr = applied_throttle_;
      const float thr_abs = std::abs(thr);
      float v_expected = 0.0f;
      if (thr_abs > f.motor_deadzone && f.motor_deadzone < 1.0f) {
        const float sign = thr < 0.0f ? -1.0f : 1.0f;
        v_expected = sign * f.motor_speed_gain * (thr_abs - f.motor_deadzone) /
                     (1.0f - f.motor_deadzone);
      }
      ctx_.ekf.UpdateSpeed(v_expected, f.speed_meas_noise);
    }

    // Неголономное ограничение vy≈0 (LOS-233): гейтим по yaw rate — при
    // явном заносе/вращении боковая скорость реальна, ограничение ослабляем.
    constexpr float kNhcMaxYawRateRps = 1.0f;  // ~57°/с
    if (f.nhc_enabled && std::abs(ctx_.ekf.GetYawRate()) < kNhcMaxYawRateRps) {
      ctx_.ekf.UpdateNonHolonomic(f.nhc_noise);
    }

    // Обновляем оценку продольного линейного ускорения для TiltEstimator
    // СЛЕДУЮЩЕГО тика: конечная разность заякоренного EKF vx этого тика
    // (потребляется выше только при motor_model_active — иначе EKF vx не
    // является независимым источником, см. комментарий у a_lin_prev_g_
    // в .hpp).
    const float vx_now = ctx_.ekf.GetVx();
    a_lin_prev_g_ = (vx_now - prev_vx_) / dt_sec / kG;
    prev_vx_ = vx_now;
  }
  // Безусловно (см. комментарий у tilt_active_this_tick выше): фиксирует
  // «tilt_est_.Update() вызывался этот тик» вне зависимости от того, через
  // какой именно путь он был пропущен.
  tilt_was_enabled_ = tilt_active_this_tick;

  if (ekf_active && sensors_.imu_enabled && sensors_.mag_enabled) {
    constexpr float kDegToRad = 3.14159265358979f / 180.0f;
    ctx_.ekf.UpdateHeading(sensors_.heading_deg * kDegToRad);
  }
}

void ControlLoopProcessor::UpdateAutoDrive(uint32_t now_ms, uint32_t dt_ms) {
  auto ad_input = BuildAutoDriveInput(sensors_, ctx_.imu_calib, dt_ms, now_ms);
  if (sensors_.imu_enabled) {
    ad_input.speed_ms = ctx_.ekf.GetSpeedMs();
  }
  auto ad_out = ctx_.auto_drive.Update(ad_input);
  if (ad_out.active) {
    commanded_throttle_ = ad_out.throttle;
    commanded_steering_ = ad_out.steering;
  }
  HandleAutoDriveCompletion(ad_out, ctx_.stab_mgr, ctx_.imu_calib,
                            ctx_.platform);
}

void ControlLoopProcessor::UpdateStabilization(uint32_t dt_ms) {
  if (!ctx_.stab_mgr) return;

  ctx_.stab_mgr->UpdateWeights(stab_cfg_, dt_ms);

  const DriveMode drive_mode = stab_cfg_.mode;
  const auto traits = DriveModeRegistry::Get(drive_mode).GetTraits();

  if (traits.apply_input_limits) {
    float kids_fwd_accel = 0.0f;
    if (sensors_.imu_enabled) {
      kids_fwd_accel = ctx_.imu_calib.GetForwardAccel(sensors_.imu_data);
    }
    ctx_.kids_processor.Process(stab_cfg_, commanded_throttle_,
                                commanded_steering_, dt_ms, kids_fwd_accel);
  }

  const float sw = ctx_.stab_mgr->GetStabilizationWeight();
  const float mw = ctx_.stab_mgr->GetModeTransitionWeight();

  if (traits.yaw_rate_active)
    ctx_.yaw_ctrl.Process(stab_cfg_, commanded_steering_, sw, mw, dt_ms,
                          commanded_throttle_ < 0.0f);
  if (traits.pitch_comp_active)
    ctx_.pitch_ctrl.Process(stab_cfg_, commanded_throttle_, sw);
  if (traits.slip_angle_active)
    ctx_.slip_ctrl.Process(stab_cfg_, commanded_throttle_, sw, mw, dt_ms);
  if (traits.oversteer_guard_active)
    ctx_.oversteer_guard.Process(stab_cfg_, commanded_throttle_, dt_ms,
                                 traits.oversteer_reduces_throttle);
}

bool ControlLoopProcessor::HandleFailsafe() {
  if (!ctx_.platform.FailsafeUpdate(sensors_.rc_active, sensors_.wifi_active)) {
    failsafe_was_active_ = false;
    return false;
  }

  commanded_throttle_ = 0.0f;
  commanded_steering_ = 0.0f;
  applied_throttle_ = 0.0f;
  applied_steering_ = 0.0f;

  // Сброс подсистем — однократно на переходе Inactive→Active.
  // Повторять каждые 2 мс бессмысленно (EKF/ПИД и так пусты), а EKF
  // при длительном failsafe может продолжать оценку без помех.
  if (!failsafe_was_active_) {
    failsafe_was_active_ = true;
    ctx_.yaw_ctrl.Reset();
    ctx_.slip_ctrl.Reset();
    ctx_.oversteer_guard.Reset();
    ctx_.kids_processor.Reset();
    ctx_.ekf.Reset();
    if (ctx_.stab_mgr) ctx_.stab_mgr->ResetWeights();
    if (ctx_.telem_mgr) ctx_.telem_mgr->ResetLastLogTime();
    ctx_.auto_drive.StopAll();
  }

  // Нейтраль удерживается каждый тик (defense-in-depth)
  ctx_.platform.SetPwmNeutral();
  return true;
}

void ControlLoopProcessor::UpdatePwm(uint32_t now, uint32_t dt_ms) {
  (void)dt_ms;
  const float steer_trim = stab_cfg_.steering_trim;
  const float thr_trim = stab_cfg_.throttle_trim;

  const DriveMode drive_mode = stab_cfg_.mode;
  const auto traits = DriveModeRegistry::Get(drive_mode).GetTraits();

  if (traits.use_slew_rate) {
    float effective_slew_thr = stab_cfg_.slew_throttle;
    if (stab_cfg_.braking_mode == BrakingMode::Brake &&
        std::abs(commanded_throttle_) < std::abs(applied_throttle_)) {
      effective_slew_thr *= stab_cfg_.brake_slew_multiplier;
    }
    UpdatePwmWithSlewRate(
        ctx_.platform, now, commanded_throttle_, commanded_steering_,
        applied_throttle_, applied_steering_, last_pwm_update_, thr_trim,
        steer_trim, effective_slew_thr, stab_cfg_.slew_steering);
  } else {
    applied_throttle_ = commanded_throttle_ + thr_trim;
    applied_steering_ = commanded_steering_ + steer_trim;
    ctx_.platform.SetPwm(applied_throttle_, applied_steering_);
  }
}

void ControlLoopProcessor::UpdateTelemetry(uint32_t now, uint32_t dt_ms) {
  (void)dt_ms;
  const TelemetryContext tctx{ctx_.ekf,
                              ctx_.madgwick,
                              ctx_.imu_calib,
                              ctx_.oversteer_guard,
                              ctx_.kids_processor,
                              ctx_.auto_drive};
  const DriveMode drive_mode = stab_cfg_.mode;

  if (ctx_.telem_handler) {
    auto snap = BuildTelemetrySnapshot(
        tctx, now, sensors_, stab_cfg_, drive_mode, applied_throttle_,
        applied_steering_, commanded_throttle_, commanded_steering_);
    // FW-RF8: failsafe в снимок — чтобы JSON строился в задаче телеметрии без
    // обращения к платформе из чужого потока.
    snap.failsafe = ctx_.platform.FailsafeIsActive();
    ctx_.telem_handler->SendTelemetry(now, snap);
  }

  if (sensors_.imu_enabled && ctx_.telem_mgr) {
    const uint32_t last_log = ctx_.telem_mgr->GetLastLogTime();
    if (now - last_log >= config::TelemetryLogConfig::kLogIntervalMs) {
      auto frame =
          BuildLogFrame(tctx, now, sensors_, applied_throttle_,
                        applied_steering_, commanded_throttle_,
                        commanded_steering_, drive_mode, stab_cfg_.enabled);
      ctx_.telem_mgr->Push(frame);
      ctx_.telem_mgr->SetLastLogTime(now);
#ifdef ESP_PLATFORM
      UdpTelemEnqueue(frame);
#endif
    }
  }
}

#ifdef RC_PROFILE_LOOP
void ControlLoopProcessor::EmitProfile(uint32_t loops) {
  if (loops == 0) return;
  {
    LogFormat fmt;
    fmt << "PROF(us/iter): comp=" << (prof_components_us_ / loops)
        << " sens=" << (prof_sensors_us_ / loops)
        << " ctrl=" << (prof_control_us_ / loops)
        << " stab=" << (prof_stab_us_ / loops)
        << " pwm=" << (prof_pwm_us_ / loops)
        << " telem=" << (prof_telem_us_ / loops)
        << " diag=" << (prof_diag_us_ / loops);
    ctx_.platform.Log(LogLevel::Info, fmt.str());
  }
  {
    // LOS-219: пиковые (worst-case) значения по стадиям + частота
    // выбросов — среднее теряет редкие однократные stall'ы (напр. от
    // синхронного NVS commit) на фоне тысяч обычных итераций за интервал.
    // step_iter — макс. время ВНУТРИ Step(); period — макс. реальный период
    // между вызовами Step() (dt_ms). Печатаем оба (код-ревью PR #297):
    // расхождение между ними указывает на stall ВНЕ Step() (напр. в
    // DelayUntilNextTick() на другом ядре) — outliers считается по period.
    LogFormat fmt;
    fmt << "PROF(max us): comp=" << prof_components_max_us_
        << " sens=" << prof_sensors_max_us_ << " ctrl=" << prof_control_max_us_
        << " stab=" << prof_stab_max_us_ << " pwm=" << prof_pwm_max_us_
        << " telem=" << prof_telem_max_us_ << " diag=" << prof_diag_max_us_
        << " step_iter=" << prof_step_max_us_
        << " period=" << prof_period_max_us_ << "  outliers=" << prof_outliers_
        << "/" << loops;
    ctx_.platform.Log(LogLevel::Info, fmt.str());
  }
  prof_components_us_ = 0;
  prof_sensors_us_ = 0;
  prof_control_us_ = 0;
  prof_stab_us_ = 0;
  prof_pwm_us_ = 0;
  prof_telem_us_ = 0;
  prof_diag_us_ = 0;
  prof_components_max_us_ = 0;
  prof_sensors_max_us_ = 0;
  prof_control_max_us_ = 0;
  prof_stab_max_us_ = 0;
  prof_pwm_max_us_ = 0;
  prof_telem_max_us_ = 0;
  prof_diag_max_us_ = 0;
  prof_step_max_us_ = 0;
  prof_period_max_us_ = 0;
  prof_outliers_ = 0;
}
#endif

}  // namespace rc_vehicle
