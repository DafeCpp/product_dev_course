#include "control_loop_processor.hpp"

#include <algorithm>
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
  ControlTickInput input{
      .now_ms = now,
      .dt_ms = dt_ms,
      .config =
          ctx_.stab_mgr ? ctx_.stab_mgr->GetConfig() : StabilizationConfig{},
  };
  // После выключения Kids speed limiter больше нет feedback-звена, которое
  // нужно исключать из якоря. Сразу возвращаемся к фактическому PWM прошлого
  // тика, до UpdateSensorsAndEkf(), иначе EKF получает один лишний
  // counterfactual-снимок на границе переключения (LOS-246).
  if (input.config.mode == DriveMode::Kids &&
      !input.config.KidsSpeedLimiterActive()) {
    persistent_.motor_model_throttle = persistent_.applied.throttle;
  }
  // Отдельный лап ДО UpdateComponents() (код-ревью PR #297): без него
  // возможное ожидание config_mutex_ в GetConfig() выше (тот же мьютекс,
  // что берёт SetConfig() из WS-потока — NVS-гипотеза LOS-219) попало бы
  // целиком в comp-стадию, ложно указывая на RC/WiFi/IMU.
  PROF_LAP(prof_cfg_us_, prof_cfg_max_us_);

  UpdateComponents(now, dt_ms);  // RC/WiFi/IMU read + Madgwick + LPF
  PROF_LAP(prof_components_us_, prof_components_max_us_);
  ControlTickState state{.command = persistent_.command};
  UpdateSensorsAndEkf(input, state);
  PROF_LAP(prof_sensors_us_, prof_sensors_max_us_);

  ProcessCalibration(now, state);

  SelectControlSource(input.sensors, state.command.throttle,
                      state.command.steering);
  UpdateAutoDrive(input, state);
  // Базовая цель для не-Kids режимов. В Kids она ниже уточняется после всех
  // не-speed стабилизаторов, но до speed limiter (LOS-246).
  state.motor_model_target_throttle = state.command.throttle;
  PROF_LAP(prof_control_us_, prof_control_max_us_);

  UpdateStabilization(input, state);
  PROF_LAP(prof_stab_us_, prof_stab_max_us_);
  // При активном failsafe UpdatePwm пропускается: иначе SetPwm(0 + trim)
  // перезаписал бы нейтраль ненулевым trim'ом — моторы ползли бы при
  // потере сигнала (FW-R1).
  if (!HandleFailsafe(input, state)) {
    UpdatePwm(input, state);
  }
  persistent_.command = state.command;
  PROF_LAP(prof_pwm_us_, prof_pwm_max_us_);
  UpdateTelemetry(input, state);
  PROF_LAP(prof_telem_us_, prof_telem_max_us_);

  {
    const DiagnosticsContext dctx{ctx_.platform,    *ctx_.stab_mgr,
                                  ctx_.madgwick,    ctx_.ekf,
                                  ctx_.imu_handler, ctx_.last_loop_hz};
#ifdef RC_PROFILE_LOOP
    const uint32_t prof_loops = diag_loop_count_;
#endif
    MaybePublishDiagnostics(dctx, input.config, now, diag_loop_count_,
                            diag_start_ms_);
#ifdef RC_PROFILE_LOOP
    // PROF_LAP/PROF_END — ДО EmitProfile() (код-ревью PR #297): EmitProfile()
    // печатает текущее окно и тут же обнуляет аккумуляторы (diag/step_iter/
    // period/outliers в том числе). Если вызвать их ПОСЛЕ EmitProfile(), эта
    // же (пограничная) итерация попала бы в отчёт per-stage максимумами
    // (записанными выше, до диагностики), но diag/step_iter/period/outliers
    // от неё же ушли бы в СЛЕДУЮЩИЙ отчёт — рассинхронизация, из-за которой
    // редкий stall на пограничной итерации давал бы противоречивые max по
    // стадиям vs по итерации целиком. Не идеально — PROF_LAP(diag) меряет
    // только MaybePublishDiagnostics() (не может измерить время самого
    // EmitProfile() до его вызова), но так хотя бы step_iter/period/outliers
    // этой итерации попадают в ТОТ ЖЕ отчёт, что и её per-stage максимумы.
    PROF_LAP(prof_diag_us_, prof_diag_max_us_);
    PROF_END();
    // diag_loop_count_ обнуляется в MaybePublishDiagnostics, когда сработал
    // интервал — это и есть сигнал напечатать средние и сбросить аккумуляторы.
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

void ControlLoopProcessor::UpdateSensorsAndEkf(ControlTickInput& input,
                                               ControlTickState& state) {
#ifdef RC_PROFILE_LOOP
  const uint64_t snapshot_start_us = ctx_.platform.GetTimeUs();
#endif
  input.sensors =
      BuildSensorSnapshot(ctx_.rc_handler, ctx_.wifi_handler, ctx_.imu_handler);
#ifdef RC_PROFILE_LOOP
  {
    const uint64_t snapshot_us = ctx_.platform.GetTimeUs() - snapshot_start_us;
    prof_snapshot_us_ += snapshot_us;
    if (snapshot_us > prof_snapshot_max_us_) {
      prof_snapshot_max_us_ = snapshot_us;
    }
  }
  const uint64_t estimator_start_us = ctx_.platform.GetTimeUs();
#endif

  const VehicleStateEstimatorInput estimator_input{
      .filter = input.config.filter,
      .dt_ms = input.dt_ms,
      .commanded_throttle = persistent_.command.throttle,
      .motor_model_throttle = persistent_.motor_model_throttle,
      .ekf_available = ctx_.stab_mgr != nullptr,
      .speed_calibration_active = ctx_.auto_drive.IsSpeedCalibActive(),
  };
  state.estimate = state_estimator_.Update(input.sensors, estimator_input);

#ifdef RC_PROFILE_LOOP
  {
    const uint64_t estimator_us =
        ctx_.platform.GetTimeUs() - estimator_start_us;
    prof_ekf_us_ += estimator_us;
    if (estimator_us > prof_ekf_max_us_) {
      prof_ekf_max_us_ = estimator_us;
    }
  }
#endif
}

void ControlLoopProcessor::ProcessCalibration(uint32_t now_ms,
                                              ControlTickState& state) {
  if (!ctx_.calib_mgr) return;

  ctx_.calib_mgr->ProcessRequest(now_ms);
  ctx_.calib_mgr->ProcessCompletion(now_ms);
  // Applied on the control-loop thread because ImuCalibration and Madgwick
  // are not thread-safe.
  ctx_.calib_mgr->ProcessForwardDirectionRequest();

  const CalibrationEffects effects = ctx_.calib_mgr->ConsumeEffects();
  if (effects.reference_frame_changed) {
    state_estimator_.OnReferenceFrameChanged();
  }
  if (effects.ekf_reset) {
    state_estimator_.RefreshEkfFields(state.estimate);
  }
}

void ControlLoopProcessor::UpdateAutoDrive(const ControlTickInput& input,
                                           ControlTickState& state) {
  auto ad_input = BuildAutoDriveInput(input.sensors, ctx_.imu_calib,
                                      input.dt_ms, input.now_ms);
  if (input.sensors.imu_enabled) {
    ad_input.speed_ms = state.estimate.speed_ms;
  }
  auto ad_out = ctx_.auto_drive.Update(ad_input);
  if (ad_out.active) {
    state.command.throttle = ad_out.throttle;
    state.command.steering = ad_out.steering;
  }
  HandleAutoDriveCompletion(ad_out, ctx_.stab_mgr, ctx_.imu_calib,
                            ctx_.platform);
}

void ControlLoopProcessor::UpdateStabilization(const ControlTickInput& input,
                                               ControlTickState& state) {
  if (!ctx_.stab_mgr) return;

  ctx_.stab_mgr->UpdateWeights(input.config, input.dt_ms);

  const ModeTraits policy =
      DriveModeRegistry::Get(input.config.mode).GetTraits();
  float pitch_deg = 0.0f;
  if (policy.pitch_comp_active && input.sensors.imu_enabled) {
    float roll_deg = 0.0f;
    float yaw_deg = 0.0f;
    ctx_.madgwick.GetEulerDeg(pitch_deg, roll_deg, yaw_deg);
  }

  constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;
  const StabilizationInput stabilization_input{
      .command = state.command,
      .dt_ms = input.dt_ms,
      .speed_ms = state.estimate.speed_ms,
      .slip_angle_deg = state.estimate.slip_angle_rad * kRadToDeg,
      .yaw_rate_rps = state.estimate.yaw_rate_rps,
      .vx_variance = ctx_.ekf.GetVxVariance(),
      .filtered_gyro_z_dps = input.sensors.filtered_gz,
      .pitch_deg = pitch_deg,
      .forward_accel_g = state.estimate.forward_accel_g,
      .stabilization_weight = ctx_.stab_mgr->GetStabilizationWeight(),
      .mode_transition_weight = ctx_.stab_mgr->GetModeTransitionWeight(),
      .imu_enabled = input.sensors.imu_enabled,
      .ekf_diverged = state.estimate.ekf_diverged,
      .speed_calibration_active = ctx_.auto_drive.IsSpeedCalibActive(),
  };
  const StabilizationOutput output = stabilization_pipeline_.Process(
      input.config, policy, stabilization_input);
  state.command = output.command;
  state.motor_model_target_throttle = output.motor_model_target_throttle;
}

bool ControlLoopProcessor::HandleFailsafe(const ControlTickInput& input,
                                          ControlTickState& state) {
  if (!ctx_.platform.FailsafeUpdate(input.sensors.rc_active,
                                    input.sensors.wifi_active)) {
    persistent_.failsafe_was_active = false;
    state.failsafe_active = false;
    return false;
  }

  state.failsafe_active = true;
  state.command = {};
  state.motor_model_target_throttle = 0.0f;
  persistent_.motor_model_throttle = 0.0f;
  persistent_.applied = {};

  // Сброс подсистем — однократно на переходе Inactive→Active.
  // Повторять каждые 2 мс бессмысленно (EKF/ПИД и так пусты), а EKF
  // при длительном failsafe может продолжать оценку без помех.
  if (!persistent_.failsafe_was_active) {
    persistent_.failsafe_was_active = true;
    ctx_.yaw_ctrl.Reset();
    ctx_.slip_ctrl.Reset();
    ctx_.oversteer_guard.Reset();
    ctx_.kids_processor.Reset();
    ctx_.ekf.Reset();
    state_estimator_.RefreshEkfFields(state.estimate);
    if (ctx_.stab_mgr) ctx_.stab_mgr->ResetWeights();
    if (ctx_.telem_mgr) ctx_.telem_mgr->ResetLastLogTime();
    ctx_.auto_drive.StopAll();
  }

  // Нейтраль удерживается каждый тик (defense-in-depth)
  ctx_.platform.SetPwmNeutral();
  return true;
}

void ControlLoopProcessor::UpdatePwm(const ControlTickInput& input,
                                     ControlTickState& state) {
  const float steer_trim = input.config.steering_trim;
  const float thr_trim = input.config.throttle_trim;

  const DriveMode drive_mode = input.config.mode;
  const auto traits = DriveModeRegistry::Get(drive_mode).GetTraits();
  // Тот же предикат, что и в KidsModeProcessor: снятый мастер-выключатель
  // ограничителей снимает и Kids-потолок slew rate (LOS-286).
  const bool kids_limiters = input.config.KidsLimitersActive();
  const bool kids_speed_limiter = input.config.KidsSpeedLimiterActive();

  if (traits.use_slew_rate) {
    const uint32_t pwm_dt_ms = input.now_ms - persistent_.last_pwm_update;
    const bool pwm_updated = pwm_dt_ms >= config::PwmConfig::kUpdateIntervalMs;
    float effective_slew_thr = input.config.slew_throttle;
    float effective_slew_steer = input.config.slew_steering;
    if (kids_limiters) {
      effective_slew_thr =
          std::min(effective_slew_thr, input.config.kids_mode.slew_throttle);
      effective_slew_steer =
          std::min(effective_slew_steer, input.config.kids_mode.slew_steering);
    }
    const float base_slew_thr = effective_slew_thr;
    if (input.config.braking_mode == BrakingMode::Brake &&
        std::abs(state.command.throttle) <
            std::abs(persistent_.applied.throttle)) {
      effective_slew_thr *= input.config.brake_slew_multiplier;
    }
    UpdatePwmWithSlewRate(ctx_.platform, input.now_ms, state.command.throttle,
                          state.command.steering, persistent_.applied.throttle,
                          persistent_.applied.steering,
                          persistent_.last_pwm_update, thr_trim, steer_trim,
                          effective_slew_thr, effective_slew_steer);

    if (kids_speed_limiter && pwm_updated) {
      // UpdatePwmWithSlewRate обновил реальный PWM на этом тике. Повторяем
      // только его математическую slew-ступень для counterfactual цели, не
      // включая speed limiter. Условие использует уже обновлённый timestamp.
      float model_slew_thr = base_slew_thr;
      if (input.config.braking_mode == BrakingMode::Brake &&
          std::abs(state.motor_model_target_throttle) <
              std::abs(persistent_.motor_model_throttle)) {
        model_slew_thr *= input.config.brake_slew_multiplier;
      }
      persistent_.motor_model_throttle = firmware_common::ApplySlewRate(
          state.motor_model_target_throttle, persistent_.motor_model_throttle,
          model_slew_thr, pwm_dt_ms / 1000.0f);
    } else if (!kids_speed_limiter) {
      persistent_.motor_model_throttle = persistent_.applied.throttle;
    }
  } else {
    persistent_.applied.throttle = state.command.throttle + thr_trim;
    persistent_.applied.steering = state.command.steering + steer_trim;
    ctx_.platform.SetPwm(persistent_.applied.throttle,
                         persistent_.applied.steering);
    persistent_.motor_model_throttle =
        kids_speed_limiter ? state.motor_model_target_throttle + thr_trim
                           : persistent_.applied.throttle;
  }
}

void ControlLoopProcessor::UpdateTelemetry(const ControlTickInput& input,
                                           const ControlTickState& state) {
  const TelemetryContext tctx{ctx_.ekf,
                              ctx_.madgwick,
                              ctx_.imu_calib,
                              ctx_.oversteer_guard,
                              ctx_.kids_processor,
                              ctx_.auto_drive};
  const DriveMode drive_mode = input.config.mode;

  if (ctx_.telem_handler) {
    auto snap = BuildTelemetrySnapshot(
        tctx, input.now_ms, input.sensors, input.config, drive_mode,
        persistent_.applied.throttle, persistent_.applied.steering,
        state.command.throttle, state.command.steering,
        state.estimate.forward_accel_g);
    // FW-RF8: failsafe в снимок — чтобы JSON строился в задаче телеметрии без
    // обращения к платформе из чужого потока.
    snap.failsafe = ctx_.platform.FailsafeIsActive();
    ctx_.telem_handler->SendTelemetry(input.now_ms, snap);
  }

  if (input.sensors.imu_enabled && ctx_.telem_mgr) {
    const uint32_t last_log = ctx_.telem_mgr->GetLastLogTime();
    if (input.now_ms - last_log >= config::TelemetryLogConfig::kLogIntervalMs) {
      auto frame = BuildLogFrame(
          tctx, input.now_ms, input.sensors, persistent_.applied.throttle,
          persistent_.applied.steering, state.command.throttle,
          state.command.steering, drive_mode, input.config.enabled);
      ctx_.telem_mgr->Push(frame);
      ctx_.telem_mgr->SetLastLogTime(input.now_ms);
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
    // LOS-219/250: разбивка sens — sensor snapshot vs estimator pipeline.
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
