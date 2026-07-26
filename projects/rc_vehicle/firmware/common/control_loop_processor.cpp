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
// PROF_START() также меряет фактический период между входами в Step() через
// GetTimeUs() (микросекунды) — код-ревью PR #297: dt_ms приходит от
// GetTimeMs() (целые мс), и period_us = dt_ms*1000 не может восстановить
// точность, потерянную квантованием до мс: период чуть выше 4-мс порога
// мог округлиться вниз до dt_ms==4 и не засчитаться как outlier. Считаем
// период тем же источником (GetTimeUs()), что и остальной профайлер —
// prof_prev_entry_us_ хранит момент входа в Step() с прошлого вызова.
#define PROF_START()                                          \
  const uint64_t _pt0 = ctx_.platform.GetTimeUs();            \
  uint64_t _pt = _pt0;                                        \
  const uint64_t _prof_period_us =                            \
      prof_prev_entry_us_ ? (_pt0 - prof_prev_entry_us_) : 0; \
  prof_prev_entry_us_ = _pt0;                                 \
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
// Не делает нового замера времени — переиспользует _prof_iter_us (сумма
// PROF_LAP-дельт за эту итерацию) и _prof_period_us (из PROF_START(),
// микросекундный период между входами в Step() — не dt_ms, см. выше).
#define PROF_END()                                                            \
  do {                                                                        \
    if (_prof_iter_us > prof_step_max_us_) prof_step_max_us_ = _prof_iter_us; \
    if (_prof_period_us > prof_period_max_us_)                                \
      prof_period_max_us_ = _prof_period_us;                                  \
    if (_prof_period_us > config::ProfilingConfig::kOutlierThresholdUs)       \
      ++prof_outliers_;                                                       \
  } while (0)
#else
#define PROF_START() ((void)0)
#define PROF_LAP(acc, max_acc) ((void)0)
#define PROF_END() ((void)0)
#endif

namespace rc_vehicle {

void ControlLoopProcessor::Step(uint32_t now, uint32_t dt_ms) {
  // PROF_START() — самая первая строка (код-ревью PR #297): GetConfig()
  // ниже берёт config_mutex_, который также берёт SetConfig() из WS-потока
  // (LOS-219, NVS-гипотеза) — если бы PROF_START() шёл после снапшота,
  // задержка на мьютексе была бы НЕВИДИМА ни для step_iter, ни для period,
  // хотя произошла бы внутри Step(), подрывая саму интерпретацию
  // расхождения period/step_iter как "stall вне Step()".
  PROF_START();

  ++diag_loop_count_;

  // Единственный snapshot конфига на итерацию (FW-RF5): одна копия под
  // мьютексом вместо трёх (Step/UpdateWeights/диагностика) на 500 Гц.
  stab_cfg_ =
      ctx_.stab_mgr ? ctx_.stab_mgr->GetConfig() : StabilizationConfig{};
  // После выключения Kids speed limiter больше нет feedback-звена, которое
  // нужно исключать из якоря. Сразу возвращаемся к фактическому PWM прошлого
  // тика, до UpdateSensorsAndEkf(), иначе EKF получает один лишний
  // counterfactual-снимок на границе переключения (LOS-246).
  if (stab_cfg_.mode == DriveMode::Kids &&
      !stab_cfg_.kids_mode.speed_limit_enabled) {
    motor_model_throttle_ = applied_throttle_;
  }
  // Отдельный лап ДО UpdateComponents() (код-ревью PR #297): без него
  // возможное ожидание config_mutex_ в GetConfig() выше (тот же мьютекс,
  // что берёт SetConfig() из WS-потока — NVS-гипотеза LOS-219) попало бы
  // целиком в comp-стадию, ложно указывая на RC/WiFi/IMU.
  PROF_LAP(prof_cfg_us_, prof_cfg_max_us_);

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
  // Базовая цель для не-Kids режимов. В Kids она ниже уточняется после всех
  // не-speed стабилизаторов, но до speed limiter (LOS-246).
  motor_model_target_throttle_ = commanded_throttle_;
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
    // PROF_LAP/PROF_END — ДО EmitProfile() (код-ревью PR #297): EmitProfile()
    // печатает текущее окно и тут же обнуляет аккумуляторы (diag/step_iter/
    // period/outliers в том числе). Если вызвать их ПОСЛЕ EmitProfile(), эта
    // же (пограничная) итерация попала бы в отчёт per-stage максимумами
    // (записанными выше, до диагностики), но diag/step_iter/period/outliers
    // от неё же ушли бы в СЛЕДУЮЩИЙ отчёт — рассинхронизация, из-за которой
    // редкий stall на пограничной итерации давал бы противоречивые max по
    // стадиям vs по итерации целиком. Не идеально — PROF_LAP(diag) меряет
    // только PrintDiagnostics() (не может измерить время самого EmitProfile()
    // до его вызова), но так хотя бы step_iter/period/outliers этой итерации
    // попадают в ТОТ ЖЕ отчёт, что и её per-stage максимумы.
    PROF_LAP(prof_diag_us_, prof_diag_max_us_);
    PROF_END();
    // diag_loop_count_ обнуляется в PrintDiagnostics, когда сработал интервал —
    // это и есть сигнал напечатать средние и сбросить аккумуляторы.
    if (diag_loop_count_ == 0) {
      EmitProfile(prof_loops);
      // Код-ревью PR #297: сами Log()-вызовы EmitProfile() занимают время,
      // которое нельзя было измерить ДО её вызова (нельзя напечатать число
      // о ещё не завершившемся событии). Без этой досчитки оно улетучилось
      // бы бесследно: PROF_END() выше уже финализировал step_iter ДО
      // EmitProfile(), а её собственная стоимость влилась бы только в
      // period СЛЕДУЮЩЕЙ итерации — период-выброс с заниженным step_iter,
      // будто stall произошёл вне Step(), хотя на деле внутри Step() этой
      // же итерации, просто после PROF_END(). Добавляем в diag/step_max
      // (уже обнулённые EmitProfile() выше) — НЕ через PROF_END() повторно,
      // чтобы не задвоить prof_period_max_us_/prof_outliers_ (period этой
      // итерации не изменился и уже учтён первым PROF_END() выше).
      const uint64_t _emit_us = ctx_.platform.GetTimeUs() - _pt;
      prof_diag_us_ += _emit_us;
      if (_emit_us > prof_diag_max_us_) prof_diag_max_us_ = _emit_us;
      // Код-ревью PR #297: сравнивать нужно с ПОЛНЫМ временем этой
      // (пограничной) итерации внутри Step() (_prof_iter_us + _emit_us),
      // а не с одним _emit_us — иначе, напр., тело в 3мс + EmitProfile()
      // в 3мс дают реальные 6мс внутри Step(), но step_iter в свежем
      // окне засеивался бы только 3мс EmitProfile(), теряя тело. period
      // СЛЕДУЮЩЕЙ итерации корректно отразил бы все 6мс — расхождение
      // period/step_iter снова ложно указывало бы на stall вне Step().
      const uint64_t _boundary_total_us = _prof_iter_us + _emit_us;
      if (_boundary_total_us > prof_step_max_us_)
        prof_step_max_us_ = _boundary_total_us;
    }
#endif
  }
}

void ControlLoopProcessor::UpdateComponents(uint32_t now, uint32_t dt_ms) {
  if (ctx_.rc_handler) ctx_.rc_handler->Update(now, dt_ms);
  if (ctx_.wifi_handler) ctx_.wifi_handler->Update(now, dt_ms);
  if (ctx_.imu_handler) ctx_.imu_handler->Update(now, dt_ms);
}

void ControlLoopProcessor::UpdateSensorsAndEkf(uint32_t dt_ms) {
#ifdef RC_PROFILE_LOOP
  // LOS-219/250: раздельные тайминги snapshot (BuildSensorSnapshot+ComOffset)
  // vs ekf (ротация/TiltEstimator/EKF-обновления ниже) — sens занимает
  // ~950-1000 мкс на реальном железе, вторая по размеру стадия после comp,
  // без этой разбивки полностью непрозрачна.
  const uint64_t _snap_t0 = ctx_.platform.GetTimeUs();
#endif
  sensors_ =
      BuildSensorSnapshot(ctx_.rc_handler, ctx_.wifi_handler, ctx_.imu_handler);
  prev_gz_rad_s_ =
      CorrectImuForComOffset(sensors_, ctx_.imu_calib, prev_gz_rad_s_, dt_ms);
#ifdef RC_PROFILE_LOOP
  {
    const uint64_t _snap_d = ctx_.platform.GetTimeUs() - _snap_t0;
    prof_snapshot_us_ += _snap_d;
    if (_snap_d > prof_snapshot_max_us_) prof_snapshot_max_us_ = _snap_d;
  }
  const uint64_t _ekf_t0 = ctx_.platform.GetTimeUs();
#endif

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
    // Вход модели — финализированный снимок прошлого тика. В Kids он проходит
    // тот же внешний PWM slew, что и фактическая команда, но исключает только
    // speed limiter: иначе limiter регулировал бы собственное измерение EKF.
    // Единый снимок не зависит от режима ТЕКУЩЕГО тика, поэтому безопасен при
    // переключении режима между тиками (LOS-246).
    if (motor_model_active) {
      const float thr = motor_model_throttle_;
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

#ifdef RC_PROFILE_LOOP
  {
    const uint64_t _ekf_d = ctx_.platform.GetTimeUs() - _ekf_t0;
    prof_ekf_us_ += _ekf_d;
    if (_ekf_d > prof_ekf_max_us_) prof_ekf_max_us_ = _ekf_d;
  }
#endif
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
                                commanded_steering_, dt_ms, fwd_accel_g_,
                                nullptr, /*apply_speed_limit=*/false);
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

  if (traits.apply_input_limits) {
    // Все обычные стабилизаторы уже отработали. Снимок — counterfactual
    // моторного входа без единственного feedback-звена, speed limiter;
    // затем limiter ограничивает фактическую PWM-команду (LOS-246).
    ctx_.kids_processor.ApplyCounterfactualSlew(stab_cfg_, commanded_throttle_,
                                                dt_ms);
    motor_model_target_throttle_ = commanded_throttle_;
    ctx_.kids_processor.ApplySpeedLimit(stab_cfg_, commanded_throttle_, dt_ms);
  }
}

bool ControlLoopProcessor::HandleFailsafe() {
  if (!ctx_.platform.FailsafeUpdate(sensors_.rc_active, sensors_.wifi_active)) {
    failsafe_was_active_ = false;
    return false;
  }

  commanded_throttle_ = 0.0f;
  commanded_steering_ = 0.0f;
  motor_model_throttle_ = 0.0f;
  motor_model_target_throttle_ = 0.0f;
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
    const uint32_t pwm_dt_ms = now - last_pwm_update_;
    const bool pwm_updated = pwm_dt_ms >= config::PwmConfig::kUpdateIntervalMs;
    float effective_slew_thr = stab_cfg_.slew_throttle;
    if (stab_cfg_.braking_mode == BrakingMode::Brake &&
        std::abs(commanded_throttle_) < std::abs(applied_throttle_)) {
      effective_slew_thr *= stab_cfg_.brake_slew_multiplier;
    }
    UpdatePwmWithSlewRate(
        ctx_.platform, now, commanded_throttle_, commanded_steering_,
        applied_throttle_, applied_steering_, last_pwm_update_, thr_trim,
        steer_trim, effective_slew_thr, stab_cfg_.slew_steering);

    if (drive_mode == DriveMode::Kids &&
        stab_cfg_.kids_mode.speed_limit_enabled && pwm_updated) {
      // UpdatePwmWithSlewRate обновил реальный PWM на этом тике. Повторяем
      // только его математическую slew-ступень для counterfactual цели, не
      // включая speed limiter. Условие использует уже обновлённый timestamp.
      float model_slew_thr = stab_cfg_.slew_throttle;
      if (stab_cfg_.braking_mode == BrakingMode::Brake &&
          std::abs(motor_model_target_throttle_) <
              std::abs(motor_model_throttle_)) {
        model_slew_thr *= stab_cfg_.brake_slew_multiplier;
      }
      motor_model_throttle_ = firmware_common::ApplySlewRate(
          motor_model_target_throttle_, motor_model_throttle_, model_slew_thr,
          pwm_dt_ms / 1000.0f);
    } else if (drive_mode != DriveMode::Kids ||
               !stab_cfg_.kids_mode.speed_limit_enabled) {
      motor_model_throttle_ = applied_throttle_;
    }
  } else {
    applied_throttle_ = commanded_throttle_ + thr_trim;
    applied_steering_ = commanded_steering_ + steer_trim;
    ctx_.platform.SetPwm(applied_throttle_, applied_steering_);
    motor_model_throttle_ =
        drive_mode == DriveMode::Kids && stab_cfg_.kids_mode.speed_limit_enabled
            ? motor_model_target_throttle_ + thr_trim
            : applied_throttle_;
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
  {
    LogFormat fmt;
    fmt << "PROF(us/iter): cfg=" << (prof_cfg_us_ / loops)
        << " comp=" << (prof_components_us_ / loops)
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
    fmt << "PROF(max us): cfg=" << prof_cfg_max_us_
        << " comp=" << prof_components_max_us_
        << " sens=" << prof_sensors_max_us_ << " ctrl=" << prof_control_max_us_
        << " stab=" << prof_stab_max_us_ << " pwm=" << prof_pwm_max_us_
        << " telem=" << prof_telem_max_us_ << " diag=" << prof_diag_max_us_
        << " step_iter=" << prof_step_max_us_
        << " period=" << prof_period_max_us_ << " outliers=" << prof_outliers_
        << "/" << loops;
    ctx_.platform.Log(LogLevel::Info, fmt.str());
  }
  if (ctx_.imu_handler) {
    // LOS-219/250: раздельные тайминги внутри "comp" — spi (только
    // platform_.ReadImu()) vs rest (калибровка/LPF/mag/Madgwick/vehicle-
    // frame) vs mag (только platform_.ReadMag(), раз в 5 тиков — проверка
    // гипотезы, что скачок rest_max вызван именно магнетометром).
    LogFormat fmt;
    fmt << "PROF(imu us): spi_avg="
        << (ctx_.imu_handler->GetProfSpiUs() / loops)
        << " spi_max=" << ctx_.imu_handler->GetProfSpiMaxUs()
        << " rest_avg=" << (ctx_.imu_handler->GetProfRestUs() / loops)
        << " rest_max=" << ctx_.imu_handler->GetProfRestMaxUs()
        << " mag_avg=" << (ctx_.imu_handler->GetProfMagUs() / loops)
        << " mag_max=" << ctx_.imu_handler->GetProfMagMaxUs();
    ctx_.platform.Log(LogLevel::Info, fmt.str());
    ctx_.imu_handler->ResetProfileStats();
  }
  {
    // LOS-219/250: разбивка sens — snapshot (BuildSensorSnapshot+ComOffset)
    // vs ekf (ротация/TiltEstimator/EKF-обновления).
    LogFormat fmt;
    fmt << "PROF(sens us): snapshot_avg=" << (prof_snapshot_us_ / loops)
        << " snapshot_max=" << prof_snapshot_max_us_
        << " ekf_avg=" << (prof_ekf_us_ / loops)
        << " ekf_max=" << prof_ekf_max_us_;
    ctx_.platform.Log(LogLevel::Info, fmt.str());
  }
  prof_cfg_us_ = 0;
  prof_components_us_ = 0;
  prof_sensors_us_ = 0;
  prof_snapshot_us_ = 0;
  prof_ekf_us_ = 0;
  prof_control_us_ = 0;
  prof_stab_us_ = 0;
  prof_pwm_us_ = 0;
  prof_telem_us_ = 0;
  prof_diag_us_ = 0;
  prof_cfg_max_us_ = 0;
  prof_components_max_us_ = 0;
  prof_sensors_max_us_ = 0;
  prof_snapshot_max_us_ = 0;
  prof_ekf_max_us_ = 0;
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
