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
#define PROF_START() uint64_t _pt = ctx_.platform.GetTimeUs()
#define PROF_LAP(acc)                              \
  do {                                             \
    const uint64_t _n = ctx_.platform.GetTimeUs(); \
    (acc) += _n - _pt;                             \
    _pt = _n;                                      \
  } while (0)
#else
#define PROF_START() ((void)0)
#define PROF_LAP(acc) ((void)0)
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
  PROF_LAP(prof_components_us_);
  UpdateSensorsAndEkf(dt_ms);  // snapshot + ComOffset + EKF
  PROF_LAP(prof_sensors_us_);

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
  PROF_LAP(prof_control_us_);

  UpdateStabilization(dt_ms);
  PROF_LAP(prof_stab_us_);
  // При активном failsafe UpdatePwm пропускается: иначе SetPwm(0 + trim)
  // перезаписал бы нейтраль ненулевым trim'ом — моторы ползли бы при
  // потере сигнала (FW-R1).
  if (!HandleFailsafe()) {
    UpdatePwm(now, dt_ms);
  }
  PROF_LAP(prof_pwm_us_);
  UpdateTelemetry(now, dt_ms);
  PROF_LAP(prof_telem_us_);

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
  constexpr float kG = 9.80665f;
  constexpr float kDegToRad = 3.14159265358979f / 180.0f;
  // ВАЖНО (код-ревью PR #302, круг 2, найдено независимо ревьюером и
  // ботом-ревьюером Codex): tilt_est_.Update() гейтится ТОЛЬКО imu_tick_valid
  // и tilt_comp_enabled — БЕЗ ekf_active. До этой правки Update() был заперт
  // внутри `if (ekf_active && ...)`, и при ekf_enabled=false (реальная,
  // переключаемая через WS/мобильное приложение настройка) tilt_est_
  // замораживался, fwd_accel_g_ ниже получал tilt_valid=false и молча
  // деградировал в GetForwardAccel() — то есть В ТОЧНОСТИ в баг, который чинит
  // LOS-245: Kids-лимитер снова ложно срабатывал на статическом наклоне.
  // TiltEstimator не нуждается в EKF для собственной работы (комплементарный
  // фильтр гироскоп+акселерометр, EKF ему только ОПЦИОНАЛЬНО поставляет
  // мотор-модельный якорь через a_lin_prev_g_/prev_vx_ — см. ниже), поэтому
  // независимость от ekf_active корректна и для Kids/телеметрии, и для
  // собственно фильтра.
  const bool imu_tick_valid = sensors_.imu_enabled && dt_ms > 0;
  const float dt_sec =
      imu_tick_valid ? static_cast<float>(dt_ms) * 0.001f : 0.0f;

  // tilt_was_enabled_ отслеживает, вызывался ли tilt_est_.Update() на
  // ПРЕДЫДУЩЕМ тике — установка отложена до конца функции (безусловно,
  // независимо от того, войдём ли вообще в блок ниже), чтобы ловить ЛЮБОЙ
  // путь, из-за которого Update() пропускается: tilt_comp_enabled=false
  // (9-й раунд PR #290), imu_enabled=false, dt_ms==0 (10-й раунд PR #290).
  // ekf_enabled=false БОЛЬШЕ НЕ в этом списке (см. выше) — во всех
  // оставшихся случаях pitch_rad_/roll_rad_ замораживаются одинаково.
  bool tilt_active_this_tick = false;

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
  //
  // Считается ВНЕ блока EKF ниже (LOS-245): результат нужен ещё и для
  // продольного ускорения Kids Mode/телеметрии, которое обязано считаться
  // на каждом тике с включённой IMU — в том числе при выключенном EKF.
  ImuData veh_imu = sensors_.imu_data;
  if (sensors_.imu_enabled) ctx_.imu_calib.RotateToVehicleFrame(veh_imu);

  // Ориентация для снятия проекции гравитации — как из ускорения перед
  // интеграцией в EKF, так и для fwd_accel_g_ (Kids-лимитер/телеметрия,
  // LOS-245) ниже. Источник по умолчанию — TiltEstimator (LOS-240):
  // комплементарный фильтр, не загрязняемый линейным ускорением (в отличие
  // от Madgwick — см. tilt_estimator.hpp).
  float pitch_rad = 0.0f, roll_rad = 0.0f;
  if (imu_tick_valid && stab_cfg_.filter.tilt_comp_enabled) {
    tilt_active_this_tick = true;
    if (!tilt_was_enabled_) {
      // Возобновление после ЛЮБОГО перерыва (см. комментарий у
      // tilt_active_this_tick выше): pitch_rad_/roll_rad_ заморожены с
      // последнего Update() — сбрасываем, иначе получим протухший
      // тангаж/крен из интервала простоя.
      tilt_est_.Reset();
    }
    // Мотор-модельный якорь (LOS-233) — единственный сигнал в системе,
    // действительно независимый от IMU/тангажа, но САМ производится EKF
    // (a_lin_prev_g_/prev_vx_ обновляются только внутри блока EKF ниже).
    // Поэтому якорь используем ТОЛЬКО пока ekf_active — иначе, при выключенном
    // EKF, a_lin_prev_g_/prev_vx_ заморожены на значении, которое было на
    // момент выключения (или на 0, если EKF не запускался вовсе), и подавать
    // их в tilt_est_.Update() было бы хуже, чем не подавать: не «унаследованная
    // неточность», а протухший на неопределённый срок сигнал.
    const bool motor_model_anchor_active =
        ekf_active && stab_cfg_.filter.motor_model_enabled &&
        !ctx_.auto_drive.IsSpeedCalibActive();
    const float a_lin_g = motor_model_anchor_active ? a_lin_prev_g_ : 0.0f;
    // Боковое (центростремительное) ускорение для roll-коррекции —
    // симметричный аналог a_lin_g для pitch (код-ревью PR #290, 6-й
    // раунд): без него устойчивый разворот с боковым ускорением ~0.2g
    // заваливал бы roll тем же путём, каким продольный разгон заваливал
    // pitch без a_lin_g. a = ω×v для тела, вращающегося вокруг Z со
    // скоростью gz и движущегося вперёд с vx: a_y = gz·vx (Y_veh,
    // veh_imu.gz — уже в СК машины, ROTATED выше).
    //
    // Гейтится ekf_active (код-ревью PR #302, круг 3, найдено ботом Codex):
    // prev_vx_ обновляется ТОЛЬКО внутри блока EKF ниже, поэтому при
    // выключенном EKF он заморожен на неопределённый срок. Раньше здесь было
    // написано, что это «лишь неточность roll-коррекции» — неверно: ay_grav
    // = imu.ay − a_lin_lat_g идёт не только в roll_acc, но и в accel_mag
    // (гейт коррекции) и в horiz = √(ay_grav² + az²), откуда берётся
    // pitch_acc = atan2(−ax_grav, horiz) — то есть протухший prev_vx_ мог
    // исказить и ТАНГАЖ тоже, включая fwd_accel_g_ и Kids-лимитер
    // (tilt_estimator.cpp, Update()). При vx=0 (типичный случай на момент
    // выключения EKF/если EKF не запускался) 0.0f и так совпадает с
    // прежним значением; расходится только если EKF выключили на ходу —
    // именно этот случай и был дырой.
    const float a_lin_lat_g =
        ekf_active ? (veh_imu.gz * kDegToRad) * prev_vx_ / kG : 0.0f;
    tilt_est_.SetParams({stab_cfg_.filter.tilt_corr_gain_hz,
                         stab_cfg_.filter.tilt_accel_gate_band_g});
    tilt_est_.Update(veh_imu, a_lin_g, a_lin_lat_g, dt_sec);
    pitch_rad = tilt_est_.GetPitchRad();
    roll_rad = tilt_est_.GetRollRad();
  }
  // Безусловно (см. комментарий у tilt_active_this_tick выше): фиксирует
  // «tilt_est_.Update() вызывался этот тик» вне зависимости от того, через
  // какой именно путь он был пропущен.
  tilt_was_enabled_ = tilt_active_this_tick;

  if (ekf_active && imu_tick_valid) {
    const auto& f = stab_cfg_.filter;
    const bool motor_model_active =
        f.motor_model_enabled && !ctx_.auto_drive.IsSpeedCalibActive();

    // pitch_rad/roll_rad уже посчитаны выше через TiltEstimator, если
    // tilt_comp_enabled. Если нет — фолбэк на Madgwick ТОЛЬКО для
    // grav-компенсации EKF (обратная совместимость); при выключенных обоих —
    // 0. Этот фолбэк НЕ используется для fwd_accel_g_ (Kids/телеметрия,
    // LOS-245) — Madgwick загрязняется линейным ускорением и заваливает
    // safety-лимитер на разгоне (см. комментарий у fwd_accel_g_ ниже).
    if (!stab_cfg_.filter.tilt_comp_enabled &&
        stab_cfg_.filter.madgwick_enabled) {
      float yaw_rad = 0.0f;
      ctx_.madgwick.GetEulerRad(pitch_rad, roll_rad, yaw_rad);
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
    // (потребляется выше только при motor_model_anchor_active — иначе EKF vx
    // не является независимым источником, см. комментарий у a_lin_prev_g_
    // в .hpp).
    const float vx_now = ctx_.ekf.GetVx();
    a_lin_prev_g_ = (vx_now - prev_vx_) / dt_sec / kG;
    prev_vx_ = vx_now;
  }

  // Продольное ускорение для accel-лимитера Kids Mode и телеметрии (LOS-245).
  // Считается ЗДЕСЬ, а не в ImuCalibration: оценщик тангажа живёт в control
  // loop, и тянуть зависимость от него в калибровку означало бы связать
  // низкоуровневый bias-слой с фильтрами ориентации. Потребители получают
  // готовое число (KidsModeProcessor::Process() уже принимает его
  // аргументом). Порядок в Step() гарантирует свежесть: UpdateSensorsAndEkf()
  // → UpdateStabilization() → UpdateTelemetry() в пределах одного тика.
  //
  // ЕДИНСТВЕННЫЙ допустимый источник тангажа здесь — TiltEstimator, и он
  // считается ВЫШЕ независимо от ekf_active (см. комментарий в начале
  // функции) — так что этот блок корректно видит tilt_active_this_tick=true
  // и при выключенном EKF, если tilt_comp_enabled. Фолбэка на Madgwick здесь
  // намеренно нет: Madgwick загрязняется линейным ускорением (ровно то, ради
  // чего заводили LOS-240) и при устойчивом разгоне сходится к
  // atan2(−ax, az), то есть «объясняет» ускорение наклоном и гасит его почти
  // полностью. Замерено на стенде (test_control_loop_processor.cpp, ровная
  // площадка, реальные 0.3 g при пороге 0.15): через Madgwick лимитер
  // слепнет за 0.8 с, через TiltEstimator — за 2.4 с. Для ДЕТСКОГО лимитера
  // ложноотрицательное срабатывание (не срезал газ, когда надо) хуже
  // ложноположительного, поэтому при выключенном tilt-фильтре честно
  // возвращаемся к прежнему горизонтальному приближению GetForwardAccel(),
  // а не к худшему из двух.
  //
  // ВАЖНО (известное ограничение): даже TiltEstimator не отличает УСТОЙЧИВОЕ
  // продольное ускорение от наклона — с постоянной времени 1/corr_gain_hz
  // (по умолчанию 2 с) он постепенно уводит его в тангаж, и лимитер перестаёт
  // его видеть. Kids-режим ловит короткие «тычки» газом (заметно меньше 2 с),
  // так что запас есть, но поведение зафиксировано тестом
  // SustainedAcceleration_LimiterFadesAsTiltAbsorbsIt — если понадобится
  // ловить длительный разгон, нужен независимый от IMU источник (мотор-модель
  // LOS-233 уже подаётся в TiltEstimator, но при постоянном газе она сама
  // сообщает о постоянной скорости).
  fwd_accel_g_ = 0.0f;
  if (sensors_.imu_enabled) {
    fwd_accel_g_ =
        ComputeForwardAccelG(ctx_.imu_calib, veh_imu, sensors_.imu_data,
                             tilt_est_.GetPitchRad(), tilt_active_this_tick);
  }

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
    // fwd_accel_g_ посчитан в UpdateSensorsAndEkf() этого же тика с учётом
    // текущего тангажа (LOS-245) — раньше здесь звался GetForwardAccel(),
    // считавший машину всегда горизонтальной.
    ctx_.kids_processor.Process(stab_cfg_, commanded_throttle_,
                                commanded_steering_, dt_ms, fwd_accel_g_);
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
    auto snap = BuildTelemetrySnapshot(tctx, now, sensors_, stab_cfg_,
                                       drive_mode, applied_throttle_,
                                       applied_steering_, commanded_throttle_,
                                       commanded_steering_, fwd_accel_g_);
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
  LogFormat fmt;
  fmt << "PROF(us/iter): comp=" << (prof_components_us_ / loops)
      << " sens=" << (prof_sensors_us_ / loops)
      << " ctrl=" << (prof_control_us_ / loops)
      << " stab=" << (prof_stab_us_ / loops)
      << " pwm=" << (prof_pwm_us_ / loops)
      << " telem=" << (prof_telem_us_ / loops);
  ctx_.platform.Log(LogLevel::Info, fmt.str());
  prof_components_us_ = 0;
  prof_sensors_us_ = 0;
  prof_control_us_ = 0;
  prof_stab_us_ = 0;
  prof_pwm_us_ = 0;
  prof_telem_us_ = 0;
}
#endif

}  // namespace rc_vehicle
