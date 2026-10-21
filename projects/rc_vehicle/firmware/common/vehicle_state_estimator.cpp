#include "vehicle_state_estimator.hpp"

#include <algorithm>
#include <cmath>

#include "control_loop_helpers.hpp"
#ifdef RC_PROFILE_LOOP
#include "vehicle_control_platform.hpp"
#endif

namespace rc_vehicle {

namespace {
constexpr float kG = 9.80665f;
constexpr float kDegToRad = 3.14159265358979f / 180.0f;
constexpr float kNhcMaxYawRateRps = 1.0f;
// Выдержка якоря TiltEstimator в постоянных времени accel-коррекции (LOS-369).
constexpr float kTiltAnchorSettleTimeConstants = 3.0f;
// Постоянная времени скорости на газ: dv/dt = (gain·thr − v)/τ, τ = 1/drag
// по подгонке на логах (профиль fitted_2026_08_02, drag 1.13 1/с). Без неё
// якорь выдавал установившуюся скорость сразу при газе, и EKF vx обгонял
// машину на 10–20 % на разгоне, а в повороте ошибка vx·r шла в занос
// (LOS-378).
constexpr float kMotorSpeedTauS = 0.9f;
// Детектор пробуксовки работает только на прямой (LOS-385): поворот Kids
// на полном руле — около 1.2 рад/с, шум на прямой — сотые.
constexpr float kWheelspinMaxYawRateRps = 0.3f;

// Установившаяся скорость мотор-модели на газ throttle [м/с].
float MotorModelSpeedMs(float throttle, const FilterConfig& filter) noexcept {
  const float throttle_abs = std::abs(throttle);
  if (throttle_abs <= filter.motor_deadzone || filter.motor_deadzone >= 1.0f) {
    return 0.0f;
  }
  const float sign = throttle < 0.0f ? -1.0f : 1.0f;
  return sign * filter.motor_speed_gain *
         (throttle_abs - filter.motor_deadzone) /
         (1.0f - filter.motor_deadzone);
}

// Шаг инерционного звена мотор-модели к target_ms.
float MotorLagStep(float speed_ms, float target_ms, float dt_sec) noexcept {
  return speed_ms +
         (target_ms - speed_ms) * dt_sec / (kMotorSpeedTauS + dt_sec);
}
}  // namespace

VehicleStateEstimate VehicleStateEstimator::Update(
    SensorSnapshot& sensors, const VehicleStateEstimatorInput& input
#ifdef RC_PROFILE_LOOP
    ,
    const VehicleControlPlatform* profile_clock
#endif
    ) noexcept {
#ifdef RC_PROFILE_LOOP
  const auto profile_call = [profile_clock](
                                VehicleStateEstimatorStageProfile& stage,
                                auto&& operation) noexcept {
    if (!profile_clock) {
      operation();
      return;
    }
    const uint64_t start_us = profile_clock->GetTimeUs();
    operation();
    stage.Record(profile_clock->GetTimeUs() - start_us);
  };
  profile_call(profile_.com_offset, [&] {
#endif
    prev_gz_rad_s_ = CorrectImuForComOffset(sensors, imu_calib_, prev_gz_rad_s_,
                                            input.dt_ms);
#ifdef RC_PROFILE_LOOP
  });
#endif

  const bool ekf_active = input.ekf_available && input.filter.ekf_enabled;
  bool motor_model_ran = false;
  const bool imu_tick_valid = sensors.imu_enabled && input.dt_ms > 0;
  const float dt_sec =
      imu_tick_valid ? static_cast<float>(input.dt_ms) * 0.001f : 0.0f;

  ImuData vehicle_imu = sensors.imu_data;
  if (sensors.imu_enabled) {
#ifdef RC_PROFILE_LOOP
    profile_call(profile_.rotate,
                 [&] { imu_calib_.RotateToVehicleFrame(vehicle_imu); });
#else
    imu_calib_.RotateToVehicleFrame(vehicle_imu);
#endif
  }

  bool tilt_active_this_tick = false;
  bool tilt_anchored_this_tick = false;
  float pitch_rad = 0.0f;
  float roll_rad = 0.0f;
  if (imu_tick_valid && input.filter.tilt_comp_enabled) {
    tilt_active_this_tick = true;
    if (!tilt_was_enabled_) {
      tilt_est_.Reset();
    }

    const bool motor_model_anchor_active =
        ekf_active && input.filter.motor_model_enabled &&
        !input.speed_calibration_active && !input.motor_model_counterfactual;
    // После перехода «без якоря → с якорем» в оценке ещё сидит наклон,
    // накопленный, пока разгон не вычитался (мотор-модель была выключена,
    // шла калибровка скорости или Kids speed limiter вёл counterfactual-газ).
    // Якорным pitch считается только после ~3 постоянных времени
    // accel-коррекции (τ = 1/corr_gain), причём считаются лишь тики, где
    // коррекция прошла гейт 1g: иначе компенсация тангажа добавляла бы газ по
    // ложному подъёму (LOS-369). После Reset() (старт, повторное включение
    // tilt) копить было нечего — выдержки нет.
    if (motor_model_anchor_active && tilt_was_enabled_ &&
        !prev_tick_anchored_) {
      anchor_settle_left_tau_ = kTiltAnchorSettleTimeConstants;
    }
    if (!motor_model_anchor_active) anchor_settle_left_tau_ = 0.0f;
    prev_tick_anchored_ = motor_model_anchor_active;
    const float a_lin_g = motor_model_anchor_active ? a_lin_prev_g_ : 0.0f;
    const float a_lin_lat_g =
        ekf_active ? (vehicle_imu.gz * kDegToRad) * prev_vx_ / kG : 0.0f;

    tilt_est_.SetParams(
        {input.filter.tilt_corr_gain_hz, input.filter.tilt_accel_gate_band_g});
#ifdef RC_PROFILE_LOOP
    profile_call(profile_.tilt, [&] {
      tilt_est_.Update(vehicle_imu, a_lin_g, a_lin_lat_g, dt_sec);
    });
#else
    tilt_est_.Update(vehicle_imu, a_lin_g, a_lin_lat_g, dt_sec);
#endif
    pitch_rad = tilt_est_.GetPitchRad();
    roll_rad = tilt_est_.GetRollRad();
    if (motor_model_anchor_active && tilt_est_.LastUpdateAccelCorrected()) {
      // Выдержка в постоянных времени: k = gain·dt — доля τ за тик, поэтому
      // смена tilt_corr_gain_hz на ходу её не обманывает.
      anchor_settle_left_tau_ = std::max(
          anchor_settle_left_tau_ - input.filter.tilt_corr_gain_hz * dt_sec,
          0.0f);
    }
    tilt_anchored_this_tick =
        motor_model_anchor_active && anchor_settle_left_tau_ <= 0.0f;
  }
  tilt_was_enabled_ = tilt_active_this_tick;

  if (ekf_active && imu_tick_valid) {
    const bool motor_model_active =
        input.filter.motor_model_enabled && !input.speed_calibration_active;

    if (!input.filter.tilt_comp_enabled && input.filter.madgwick_enabled) {
      float yaw_rad = 0.0f;
      madgwick_.GetEulerRad(pitch_rad, roll_rad, yaw_rad);
    }

    // Match the motor model's definition of motion-capable output. The caller
    // supplies the last value actually sent to the motor, including slew and
    // trim, so ZUPT cannot engage while a stale higher PWM is still applied.
    const float applied_throttle_abs = std::abs(input.applied_throttle);
    const float zupt_throttle_abs =
        applied_throttle_abs > input.filter.motor_deadzone
            ? applied_throttle_abs
            : 0.0f;
#ifdef RC_PROFILE_LOOP
    profile_call(profile_.imu, [&] {
#endif
      ekf_.UpdateFromImu(vehicle_imu.ax, vehicle_imu.ay, vehicle_imu.az,
                         sensors.filtered_gz, dt_sec, zupt_throttle_abs,
                         pitch_rad, roll_rad);
#ifdef RC_PROFILE_LOOP
    });
#endif

    if (motor_model_active) {
      const float expected_speed =
          MotorModelSpeedMs(input.motor_model_throttle, input.filter);
      motor_model_target_ms_ = expected_speed;
      // Инерционное звено стартует с текущей оценки: после паузы якоря
      // (калибровка скорости, сброс EKF) машина могла уже ехать.
      if (!motor_model_was_active_) motor_model_speed_ms_ = ekf_.GetVx();
      motor_model_speed_ms_ =
          MotorLagStep(motor_model_speed_ms_, expected_speed, dt_sec);
      motor_model_ran = true;
#ifdef RC_PROFILE_LOOP
      profile_call(profile_.speed, [&] {
#endif
        ekf_.UpdateSpeed(motor_model_speed_ms_, input.filter.speed_meas_noise);
#ifdef RC_PROFILE_LOOP
      });
#endif
    }

    if (input.filter.nhc_enabled &&
        std::abs(ekf_.GetYawRate()) < kNhcMaxYawRateRps) {
#ifdef RC_PROFILE_LOOP
      profile_call(profile_.nhc, [&] {
#endif
        ekf_.UpdateNonHolonomic(input.filter.nhc_noise);
#ifdef RC_PROFILE_LOOP
      });
#endif
    }

    const float vx_now = ekf_.GetVx();
    a_lin_prev_g_ = (vx_now - prev_vx_) / dt_sec / kG;
    prev_vx_ = vx_now;
  }

  motor_model_was_active_ = motor_model_ran;

  float forward_accel_g = 0.0f;
  if (sensors.imu_enabled) {
    forward_accel_g =
        ComputeForwardAccelG(imu_calib_, vehicle_imu, sensors.imu_data,
                             tilt_est_.GetPitchRad(), tilt_active_this_tick);
  }

  if (ekf_active && sensors.imu_enabled && sensors.mag_enabled &&
      !sensors.mag_rejected &&
      sensors.mag_sample_sequence != last_mag_sample_sequence_) {
#ifdef RC_PROFILE_LOOP
    profile_call(profile_.heading, [&] {
#endif
      ekf_.UpdateHeading(sensors.heading_deg * kDegToRad);
#ifdef RC_PROFILE_LOOP
    });
#endif
    last_mag_sample_sequence_ = sensors.mag_sample_sequence;
  }

  // Скорость колёс для детектора пробуксовки — мотор-модель на фактическом
  // PWM, а не на газе якоря: Kids speed limiter срезает PWM, и якорь ведёт
  // counterfactual-газ выше реального (LOS-246). Она зависит только от
  // истории газа и сбросом EKF не обнуляется: после сброса на ходу колёса
  // начинали бы с нуля, и корпус «отставал» бы от них (LOS-385).
  const float wheel_target_ms =
      MotorModelSpeedMs(input.applied_throttle, input.filter);
  if (imu_tick_valid) {
    wheel_speed_ms_ = MotorLagStep(wheel_speed_ms_, wheel_target_ms, dt_sec);
  }

  // Пробуксовка: мотор-модель за скорость колёс против интеграла ускорения
  // корпуса (LOS-385). Без мотор-модели скорость колёс неизвестна. В
  // повороте продольное ускорение кузова — это v̇x − vy·r, и без верного vy
  // интеграл уходил бы вниз (в SIL до 0.09 в повороте на асфальте); занос
  // там ловит угол заноса EKF, поэтому детектор ждёт выхода на прямую.
  if (motor_model_ran &&
      std::abs(ekf_.GetYawRate()) < kWheelspinMaxYawRateRps) {
    wheelspin_.Update(wheel_speed_ms_, wheel_target_ms, forward_accel_g,
                      dt_sec);
  } else {
    wheelspin_.Reset(wheel_speed_ms_);
  }

  return BuildEstimate(sensors.imu_enabled, tilt_active_this_tick,
                       tilt_anchored_this_tick, pitch_rad, roll_rad,
                       forward_accel_g);
}

void VehicleStateEstimator::OnReferenceFrameChanged() noexcept {
  tilt_est_.Reset();
  prev_vx_ = ekf_.GetVx();
  a_lin_prev_g_ = 0.0f;
  last_mag_sample_sequence_ = 0;
  tilt_was_enabled_ = false;
  prev_tick_anchored_ = false;
  anchor_settle_left_tau_ = 0.0f;
}

void VehicleStateEstimator::OnEkfReset() noexcept {
  motor_model_was_active_ = false;
  wheelspin_.Reset(wheel_speed_ms_);
}

void VehicleStateEstimator::RefreshEkfFields(
    VehicleStateEstimate& estimate) const noexcept {
  estimate.yaw_rad = ekf_.GetYawRad();
  estimate.vx_ms = ekf_.GetVx();
  estimate.vy_ms = ekf_.GetVy();
  estimate.speed_ms = ekf_.GetSpeedMs();
  estimate.yaw_rate_rps = ekf_.GetYawRate();
  estimate.slip_angle_rad = ekf_.GetSlipAngleRad();
  estimate.zupt_status = ekf_.GetZuptStatus();
  estimate.ekf_diverged = ekf_.IsDiverged();
}

VehicleStateEstimate VehicleStateEstimator::BuildEstimate(
    bool imu_valid, bool tilt_valid, bool tilt_accel_anchored, float pitch_rad,
    float roll_rad, float forward_accel_g) const noexcept {
  VehicleStateEstimate estimate;
  estimate.imu_valid = imu_valid;
  estimate.tilt_valid = tilt_valid;
  estimate.tilt_accel_anchored = tilt_accel_anchored;
  estimate.pitch_rad = pitch_rad;
  estimate.roll_rad = roll_rad;
  estimate.forward_accel_g = forward_accel_g;
  estimate.wheel_slip_ratio = wheelspin_.GetSlipRatio();
  estimate.wheels_driven = wheelspin_.IsDriving();
  estimate.ground_speed_ms = wheelspin_.GetGroundSpeedMs();
  RefreshEkfFields(estimate);
  return estimate;
}

}  // namespace rc_vehicle
