#include "control_components.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

#include "cJSON.h"
#include "config.hpp"
#include "imu_calibration.hpp"
#include "madgwick_filter.hpp"
#include "mag_calibration.hpp"

namespace rc_vehicle {

namespace {

const char* ZuptStatusToString(ZuptStatus status) {
  switch (status) {
    case ZuptStatus::NotEvaluated:
      return "not_evaluated";
    case ZuptStatus::Applied:
      return "applied";
    case ZuptStatus::ThrottleRejected:
      return "throttle_rejected";
    case ZuptStatus::AccelRejected:
      return "accel_rejected";
    case ZuptStatus::GyroRejected:
      return "gyro_rejected";
  }
  return "unknown";
}

}  // namespace

// ═════════════════════════════════════════════════════════════════════════
// RcInputHandler
// ═════════════════════════════════════════════════════════════════════════

void RcInputHandler::Update(uint32_t now_ms, [[maybe_unused]] uint32_t dt_ms) {
  // Опрос RC с заданной частотой
  if (now_ms - last_poll_ms_ < poll_interval_ms_) {
    return;
  }
  last_poll_ms_ = now_ms;

  // Получить команду от RC-приёмника
  last_command_ = platform_.GetRc();
  active_ = last_command_.has_value();
}

// ═════════════════════════════════════════════════════════════════════════
// WifiCommandHandler
// ═════════════════════════════════════════════════════════════════════════

void WifiCommandHandler::Update(uint32_t now_ms,
                                [[maybe_unused]] uint32_t dt_ms) {
  // Попытаться получить команду из очереди
  auto cmd = platform_.TryReceiveWifiCommand();
  if (cmd) {
    last_command_ = cmd;
    last_cmd_ms_ = now_ms;
  }
  // Кэшируем active-состояние: стабильно в пределах одной итерации control loop
  active_ = last_cmd_ms_ != 0 && (now_ms - last_cmd_ms_) < timeout_ms_;
}

// ═════════════════════════════════════════════════════════════════════════
// ImuHandler
// ═════════════════════════════════════════════════════════════════════════

void ImuHandler::Update(uint32_t now_ms, [[maybe_unused]] uint32_t dt_ms) {
  if (!enabled_) {
    return;
  }

  // Чтение IMU с заданной частотой
  if (now_ms - last_read_ms_ < read_interval_ms_) {
    return;
  }

  const uint32_t prev_read_ms = last_read_ms_;
  last_read_ms_ = now_ms;

  // Прочитать данные IMU
#ifdef RC_PROFILE_LOOP
  // LOS-219/250: время ТОЛЬКО SPI-транзакции(й), отдельно от остального
  // (калибровка/LPF/mag/Madgwick) — см. GetProfSpiUs() в .hpp. Меряем даже
  // при неудаче: сама транзакция уже отработала.
  const uint64_t _spi_t0 = platform_.GetTimeUs();
#endif
  auto imu_data = platform_.ReadImu();
#ifdef RC_PROFILE_LOOP
  {
    const uint64_t _spi_d = platform_.GetTimeUs() - _spi_t0;
    prof_spi_us_ += _spi_d;
    if (_spi_d > prof_spi_max_us_) prof_spi_max_us_ = _spi_d;
  }
#endif
  if (!imu_data) {
    return;
  }

#ifdef RC_PROFILE_LOOP
  const uint64_t _rest_t0 = platform_.GetTimeUs();
#endif
  data_ = *imu_data;

  // Подача семпла в калибровку (если идёт сбор)
  calib_.FeedSample(data_);

  // Сохранить сырые данные акселерометра ДО коррекции bias.
  // Madgwick-фильтр должен видеть истинное направление гравитации в СК датчика,
  // совпадающее с gravity_vec из калибровки. Accel bias включает компоненты
  // наклона (ax,ay mean), из-за чего bias-corrected данные = [0,0,±1]
  // не соответствуют реальному gravity_vec при наклонном монтаже.
  const float raw_ax = data_.ax, raw_ay = data_.ay, raw_az = data_.az;

  // Применить компенсацию bias (если калибровка валидна)
  calib_.Apply(data_);

  // LPF инициализирован в конструкторе — горячий путь без проверок
  filtered_gz_ = lpf_gyro_z_.Step(data_.gz);

  const float dt_sec =
      first_read_ ? (read_interval_ms_ / 1000.0f)
                  : (static_cast<float>(now_ms - prev_read_ms) / 1000.0f);
  first_read_ = false;

  // UpdateMagAndHeading + FeedMadgwick — ДО UpdateVehicleFrame(): иначе на
  // тике, где калибровка становится валидной ровно в момент истечения
  // таймаута устаревшего магнетометра, SetVehicleFrame() прочитал бы
  // filter_.yaw_has_absolute_ref_ ещё с ПРЕДЫДУЩЕГО тика (mag_enabled_ тогда
  // ещё не был инвалидирован) и мог бы закрепить курс, посчитанный по уже
  // замороженному mag-семплу (review r3629768456, LOS-229). В этом порядке
  // к моменту UpdateVehicleFrame() флаг уже отражает состояние текущего
  // тика.
  UpdateMagAndHeading(now_ms);
  FeedMadgwick(raw_ax, raw_ay, raw_az, dt_sec);

  UpdateVehicleFrame();

#ifdef RC_PROFILE_LOOP
  {
    const uint64_t _rest_d = platform_.GetTimeUs() - _rest_t0;
    prof_rest_us_ += _rest_d;
    if (_rest_d > prof_rest_max_us_) prof_rest_max_us_ = _rest_d;
  }
#endif
}

void ImuHandler::UpdateVehicleFrame() {
  // Настроить опорную СК фильтра — только при смене состояния калибровки,
  // чтобы не сбрасывать кватернион Мэджвика каждые 2 мс.
  const bool calib_valid = calib_.IsValid();
  if (calib_valid && !veh_frame_set_) {
    const auto& calib_data = calib_.GetData();
    filter_.SetVehicleFrame(calib_data.gravity_vec,
                            calib_data.accel_forward_vec, true);
    veh_frame_set_ = true;
  } else if (!calib_valid && veh_frame_set_) {
    filter_.SetVehicleFrame(nullptr, nullptr, false);
    veh_frame_set_ = false;
  }
}

void ImuHandler::UpdateMagAndHeading(uint32_t now_ms) {
  // Таймаут устаревания — ДО throttle-гейта опроса ниже и на каждом вызове
  // (IMU-тик, 2 мс), а не только на тиках, где реально идёт опрос
  // магнетометра (100 Гц): иначе IMU-тики между опросами могут уже
  // превысить kMagStaleTimeoutMs по факту, но mag_enabled_ останется true
  // до следующего 10-мс опроса — до 8 мс лишнего доверия к замороженному
  // семплу, в течение которых калибровка могла бы закрепить устаревший курс
  // (review r3629933116, LOS-229).
  if (mag_enabled_.load(std::memory_order_relaxed) &&
      (now_ms - last_mag_success_ms_) > kMagStaleTimeoutMs) {
    mag_enabled_.store(false, std::memory_order_relaxed);
  }

  // Читаем магнетометр на 100 Hz (MMC5983 CMM rate).
  // I2C/SPI транзакция ~350 мкс — не читаем каждые 2 мс.
  if ((now_ms - last_mag_read_ms_) < kMagReadIntervalMs) {
    return;
  }
  last_mag_read_ms_ = now_ms;

#ifdef RC_PROFILE_LOOP
  const uint64_t _mag_t0 = platform_.GetTimeUs();
#endif
  const auto mag_opt = platform_.ReadMag();
#ifdef RC_PROFILE_LOOP
  {
    const uint64_t _mag_d = platform_.GetTimeUs() - _mag_t0;
    prof_mag_us_ += _mag_d;
    if (_mag_d > prof_mag_max_us_) prof_mag_max_us_ = _mag_d;
  }
#endif
  if (!mag_opt) {
    return;
  }
  last_mag_success_ms_ = now_ms;
  mag_data_ = *mag_opt;
  mag_enabled_.store(true, std::memory_order_relaxed);

  // Подача нового семпла в калибровку (если идёт сбор)
  if (mag_calib_ && mag_calib_->IsCollecting()) {
    mag_calib_->FeedSample(mag_data_);
  }

  // Калиброванное значение кэшируется: FeedMadgwick использует его на
  // каждом тике 500 Гц до следующего mag-семпла (Apply детерминирован —
  // результат тот же, что пересчёт каждые 2 мс).
  mag_calibrated_ = mag_data_;
  const bool have_calib = mag_calib_ && mag_calib_->IsValid();
  if (have_calib) {
    mag_calib_->Apply(mag_calibrated_);
  }

  if (have_calib) {
    heading_deg_ = ComputePcaHeadingDeg(mag_calibrated_);
  } else {
    // Нет калибровки — fallback: простой atan2 без проекции
    const float h = std::atan2(mag_calibrated_.my, mag_calibrated_.mx) *
                    (180.f / 3.14159265f);
    heading_deg_ = (h < 0.f) ? h + 360.f : h;
  }

  // Установить опорный курс при первом валидном чтении (или после сброса)
  if (!heading_ref_set_) {
    heading_ref_ = heading_deg_;
    heading_ref_set_ = true;
  }
}

float ImuHandler::ComputePcaHeadingDeg(const MagData& mag_cal) const {
  // ── PCA heading: проекция на калибровочную плоскость ──────────────────
  const auto& cd = mag_calib_->GetData();

  // Проекция mag на горизонтальную плоскость (перпендикулярную normal)
  const float dot_n = mag_cal.mx * cd.normal[0] + mag_cal.my * cd.normal[1] +
                      mag_cal.mz * cd.normal[2];
  const float px = mag_cal.mx - dot_n * cd.normal[0];
  const float py = mag_cal.my - dot_n * cd.normal[1];
  const float pz = mag_cal.mz - dot_n * cd.normal[2];

  const float comp1 = px * cd.basis1[0] + py * cd.basis1[1] + pz * cd.basis1[2];
  const float comp2 = px * cd.basis2[0] + py * cd.basis2[1] + pz * cd.basis2[2];

  const float h = std::atan2(comp2, comp1) * (180.f / 3.14159265f);
  return (h < 0.f) ? h + 360.f : h;
}

void ImuHandler::FeedMadgwick(float raw_ax, float raw_ay, float raw_az,
                              float dt_sec) {
  if (!madgwick_enabled_) {
    // Кватернион заморожен (ни Update(), ни UpdateWithMag() не вызываются),
    // но машина могла продолжать двигаться, пока Мэджвик выключен
    // (StabilizationManager::ApplyToFilters переключает это на ходу через
    // cfg.filter.madgwick_enabled). Опору для будущей калибровки нужно
    // заработать заново после повторного включения — иначе
    // IMU/Forward-калибровка могла бы сохранить курс, посчитанный по уже
    // неактуальному кватерниону (review r3630915899, LOS-229).
    filter_.InvalidateYawTrust();
    return;
  }

  if (mag_enabled_.load(std::memory_order_relaxed)) {
    // 9DOF: полный калиброванный mag-вектор в СК датчика (FW-R3).
    // Подаётся каждый тик (включая тики без нового семпла) — предотвращает
    // дрейф yaw между обновлениями магнитометра. Madgwick сам устраняет
    // склонение (bx = sqrt(hx²+hy²)), предварительная проекция не нужна.
    filter_.UpdateWithMag(raw_ax, raw_ay, raw_az, data_.gx, data_.gy, data_.gz,
                          mag_calibrated_.mx, mag_calibrated_.my,
                          mag_calibrated_.mz, dt_sec);
  } else {
    // 6DOF: сырой акселерометр + калиброванный гироскоп
    filter_.Update(raw_ax, raw_ay, raw_az, data_.gx, data_.gy, data_.gz,
                   dt_sec);
  }
}

float ImuHandler::GetRelativeHeadingDeg() const noexcept {
  float delta = heading_deg_ - heading_ref_;
  if (delta > 180.f) delta -= 360.f;
  if (delta <= -180.f) delta += 360.f;
  return delta;
}

void ImuHandler::SetLpfCutoff(float cutoff_hz) {
  if (cutoff_hz < config::LpfConfig::kMinCutoffHz ||
      cutoff_hz > config::LpfConfig::kMaxCutoffHz) {
    return;  // Игнорировать невалидные значения
  }
  const float fs_hz = 1000.f / static_cast<float>(read_interval_ms_);
  lpf_gyro_z_.SetParams(cutoff_hz, fs_hz);
  lpf_gyro_z_.Reset();  // Сброс состояния при изменении параметров
}

// ═════════════════════════════════════════════════════════════════════════
// TelemetryHandler
// ═════════════════════════════════════════════════════════════════════════

void TelemetryHandler::SendTelemetry(uint32_t now_ms,
                                     const TelemetrySnapshot& snap) {
  if (now_ms - last_send_ms_ < send_interval_ms_) {
    return;
  }
  last_send_ms_ = now_ms;

  // FW-RF8: control loop публикует только лёгкий POD-снимок в очередь платформы
  // (детерминированно, без кучи). Построение JSON (cJSON) и отправка по WS
  // выполняются в задаче телеметрии — стоимость/сбой телеметрии физически не
  // достигает 500 Гц цикла.
  //
  // FW-R13: доставку по-прежнему НЕ гейтим по GetWebSocketClientCount() —
  // решение принимает транспорт (WebSocketSendTelem шлёт только реальным
  // WS-fd).
  platform_.PublishTelem(snap);
}

std::string BuildTelemJson(const TelemetrySnapshot& snap) {
  cJSON* root = cJSON_CreateObject();
  if (!root) return "{}";

  cJSON_AddStringToObject(root, "type", "telem");
  // Для совместимости: "mcu_pong_ok" = "контроллер жив"
  cJSON_AddBoolToObject(root, "mcu_pong_ok", true);
  cJSON_AddNumberToObject(root, "uptime_ms", snap.uptime_ms);

  // Link status
  cJSON* link = cJSON_AddObjectToObject(root, "link");
  if (link) {
    cJSON_AddBoolToObject(link, "rc_ok", snap.rc_ok);
    cJSON_AddBoolToObject(link, "wifi_ok", snap.wifi_ok);
    cJSON_AddBoolToObject(link, "failsafe", snap.failsafe);
  }

  // IMU data (если включен)
  if (snap.imu_enabled) {
    cJSON* imu = cJSON_AddObjectToObject(root, "imu");
    if (imu) {
      cJSON_AddNumberToObject(imu, "ax", snap.imu_data.ax);
      cJSON_AddNumberToObject(imu, "ay", snap.imu_data.ay);
      cJSON_AddNumberToObject(imu, "az", snap.imu_data.az);
      cJSON_AddNumberToObject(imu, "gx", snap.imu_data.gx);
      cJSON_AddNumberToObject(imu, "gy", snap.imu_data.gy);
      cJSON_AddNumberToObject(imu, "gz", snap.imu_data.gz);
      cJSON_AddNumberToObject(imu, "gyro_z_filtered", snap.filtered_gz);
      cJSON_AddNumberToObject(imu, "forward_accel", snap.forward_accel);

      // Orientation (Madgwick)
      cJSON* orientation = cJSON_AddObjectToObject(imu, "orientation");
      if (orientation) {
        cJSON_AddNumberToObject(orientation, "pitch", snap.pitch_deg);
        cJSON_AddNumberToObject(orientation, "roll", snap.roll_deg);
        cJSON_AddNumberToObject(orientation, "yaw", snap.yaw_deg);
      }
    }

    // Calibration status
    cJSON* calib = cJSON_AddObjectToObject(root, "calib");
    if (calib) {
      const char* status_str = "unknown";
      switch (snap.calib_status) {
        case CalibStatus::Idle:
          status_str = "idle";
          break;
        case CalibStatus::Collecting:
          status_str = "collecting";
          break;
        case CalibStatus::Done:
          status_str = "done";
          break;
        case CalibStatus::Failed:
          status_str = "failed";
          break;
      }
      cJSON_AddStringToObject(calib, "status", status_str);
      cJSON_AddNumberToObject(calib, "stage", snap.calib_stage);
      cJSON_AddBoolToObject(calib, "valid", snap.calib_valid);

      if (snap.calib_valid) {
        const auto& cd = snap.calib_data;
        cJSON* bias = cJSON_AddObjectToObject(calib, "bias");
        if (bias) {
          cJSON_AddNumberToObject(bias, "gx", cd.gyro_bias[0]);
          cJSON_AddNumberToObject(bias, "gy", cd.gyro_bias[1]);
          cJSON_AddNumberToObject(bias, "gz", cd.gyro_bias[2]);
          cJSON_AddNumberToObject(bias, "ax", cd.accel_bias[0]);
          cJSON_AddNumberToObject(bias, "ay", cd.accel_bias[1]);
          cJSON_AddNumberToObject(bias, "az", cd.accel_bias[2]);
        }
        cJSON* gravity = cJSON_AddArrayToObject(calib, "gravity_vec");
        if (gravity) {
          cJSON_AddItemToArray(gravity, cJSON_CreateNumber(cd.gravity_vec[0]));
          cJSON_AddItemToArray(gravity, cJSON_CreateNumber(cd.gravity_vec[1]));
          cJSON_AddItemToArray(gravity, cJSON_CreateNumber(cd.gravity_vec[2]));
        }
        cJSON* forward = cJSON_AddArrayToObject(calib, "forward_vec");
        if (forward) {
          cJSON_AddItemToArray(forward,
                               cJSON_CreateNumber(cd.accel_forward_vec[0]));
          cJSON_AddItemToArray(forward,
                               cJSON_CreateNumber(cd.accel_forward_vec[1]));
          cJSON_AddItemToArray(forward,
                               cJSON_CreateNumber(cd.accel_forward_vec[2]));
        }
      }
    }

    // Магнетометр
    if (snap.mag_enabled) {
      cJSON* mag = cJSON_AddObjectToObject(root, "mag");
      if (mag) {
        cJSON_AddNumberToObject(mag, "mx", snap.mag_data.mx);
        cJSON_AddNumberToObject(mag, "my", snap.mag_data.my);
        cJSON_AddNumberToObject(mag, "mz", snap.mag_data.mz);
        cJSON_AddNumberToObject(mag, "heading_deg", snap.heading_deg);
        cJSON_AddNumberToObject(mag, "heading_rel_deg", snap.heading_rel_deg);
      }
    }

    // EKF: динамическое состояние (vx, vy, r, slip angle)
    if (snap.ekf_available) {
      cJSON* ekf = cJSON_AddObjectToObject(root, "ekf");
      if (ekf) {
        cJSON_AddNumberToObject(ekf, "vx", snap.ekf_vx);
        cJSON_AddNumberToObject(ekf, "vy", snap.ekf_vy);
        cJSON_AddNumberToObject(ekf, "yaw_rate", snap.ekf_yaw_rate);
        cJSON_AddNumberToObject(ekf, "slip_deg", snap.ekf_slip_deg);
        cJSON_AddNumberToObject(ekf, "speed_ms", snap.ekf_speed_ms);
        cJSON_AddNumberToObject(ekf, "vx_var", snap.ekf_vx_var);
        cJSON_AddNumberToObject(ekf, "vy_var", snap.ekf_vy_var);
        cJSON_AddNumberToObject(ekf, "r_var", snap.ekf_r_var);
        cJSON_AddStringToObject(ekf, "zupt_status",
                                ZuptStatusToString(snap.ekf_zupt_status));
        cJSON_AddNumberToObject(ekf, "speed_meas", snap.ekf_speed_meas);
        cJSON_AddBoolToObject(ekf, "diverged", snap.ekf_diverged);
      }
    }

    // Oversteer warning (Phase 4.2)
    if (snap.oversteer_available) {
      cJSON* warn = cJSON_AddObjectToObject(root, "warn");
      if (warn) {
        cJSON_AddBoolToObject(warn, "oversteer", snap.oversteer_active);
      }
    }
  }

  // Kids Mode status
  if (snap.kids_mode_active) {
    cJSON* kids = cJSON_AddObjectToObject(root, "kids_mode");
    if (kids) {
      cJSON_AddBoolToObject(kids, "active", true);
      cJSON_AddBoolToObject(kids, "anti_spin_active",
                            snap.kids_anti_spin_active);
      cJSON_AddNumberToObject(kids, "throttle_limit", snap.kids_throttle_limit);
    }
  }

  // RC input (сырые значения с пульта)
  if (snap.rc_ok) {
    cJSON* rc = cJSON_AddObjectToObject(root, "rc");
    if (rc) {
      cJSON_AddNumberToObject(rc, "throttle", snap.rc_throttle);
      cJSON_AddNumberToObject(rc, "steering", snap.rc_steering);
    }
  }

  // Commanded (до trim/slew)
  cJSON* cmd = cJSON_AddObjectToObject(root, "cmd");
  if (cmd) {
    cJSON_AddNumberToObject(cmd, "throttle", snap.cmd_throttle);
    cJSON_AddNumberToObject(cmd, "steering", snap.cmd_steering);
  }

  // Actuators (после trim/slew)
  cJSON* act = cJSON_AddObjectToObject(root, "act");
  if (act) {
    cJSON_AddNumberToObject(act, "throttle", snap.throttle);
    cJSON_AddNumberToObject(act, "steering", snap.steering);
  }

  char* str = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!str) return "{}";
  std::string result(str);
  free(str);
  return result;
}

}  // namespace rc_vehicle
