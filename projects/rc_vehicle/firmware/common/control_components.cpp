#include "control_components.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

#include "mag_calibration.hpp"

#include "cJSON.h"
#include "config.hpp"
#include "imu_calibration.hpp"
#include "madgwick_filter.hpp"

namespace rc_vehicle {

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
  auto imu_data = platform_.ReadImu();
  if (!imu_data) {
    return;
  }

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

  // Обновить фильтр Madgwick: сырой акселерометр + калиброванный гироскоп.
  // Gyro bias уже вычтен в Apply(), но accel нужен сырой (см. выше).
  const float dt_sec = first_read_
                           ? (read_interval_ms_ / 1000.0f)
                           : (static_cast<float>(now_ms - prev_read_ms) / 1000.0f);
  first_read_ = false;

  // Читаем магнетометр на 100 Hz (MMC5983 CMM rate).
  // I2C/SPI транзакция ~350 мкс — не читаем каждые 2 мс.
  bool new_mag_sample = false;
  if ((now_ms - last_mag_read_ms_) >= kMagReadIntervalMs) {
    last_mag_read_ms_ = now_ms;
    const auto mag_opt = platform_.ReadMag();
    if (mag_opt) {
      mag_data_ = *mag_opt;
      mag_enabled_ = true;
      new_mag_sample = true;
    }
  }

  if (mag_enabled_) {
    MagData mag_cal = mag_data_;
    const bool have_calib = mag_calib_ && mag_calib_->IsValid();
    if (have_calib) {
      mag_calib_->Apply(mag_cal);
    }

    if (new_mag_sample) {
      // Подача нового семпла в калибровку (если идёт сбор)
      if (mag_calib_ && mag_calib_->IsCollecting()) {
        mag_calib_->FeedSample(mag_data_);
      }

      if (have_calib) {
        // ── PCA heading: проекция на калибровочную плоскость ────────────
        const auto& cd = mag_calib_->GetData();

        // Проекция mag на горизонтальную плоскость (перпендикулярную normal)
        const float dot_n = mag_cal.mx * cd.normal[0] +
                            mag_cal.my * cd.normal[1] +
                            mag_cal.mz * cd.normal[2];
        const float px = mag_cal.mx - dot_n * cd.normal[0];
        const float py = mag_cal.my - dot_n * cd.normal[1];
        const float pz = mag_cal.mz - dot_n * cd.normal[2];

        const float comp1 = px * cd.basis1[0] + py * cd.basis1[1] + pz * cd.basis1[2];
        const float comp2 = px * cd.basis2[0] + py * cd.basis2[1] + pz * cd.basis2[2];

        const float h = std::atan2(comp2, comp1) * (180.f / 3.14159265f);
        heading_deg_ = (h < 0.f) ? h + 360.f : h;

        // Подать mag в Madgwick 9DOF, повёрнутый в систему basis:
        // basis1 ≈ body X, basis2 ≈ body Y, normal ≈ body Z
        if (madgwick_enabled_) {
          const float rmx = comp1;
          const float rmy = comp2;
          const float rmz = dot_n;
          filter_.UpdateWithMag(raw_ax, raw_ay, raw_az, data_.gx, data_.gy,
                                data_.gz, rmx, rmy, rmz, dt_sec);
        }
      } else {
        // Нет калибровки — fallback: простой atan2 без проекции
        const float h = std::atan2(mag_cal.my, mag_cal.mx) * (180.f / 3.14159265f);
        heading_deg_ = (h < 0.f) ? h + 360.f : h;

        // 9DOF с сырыми mag данными (лучше чем 6DOF, но heading неточный)
        if (madgwick_enabled_) {
          filter_.UpdateWithMag(raw_ax, raw_ay, raw_az, data_.gx, data_.gy,
                                data_.gz, mag_cal.mx, mag_cal.my, mag_cal.mz,
                                dt_sec);
        }
      }

      // Установить опорный курс при первом валидном чтении (или после сброса)
      if (!heading_ref_set_) {
        heading_ref_ = heading_deg_;
        heading_ref_set_ = true;
      }
    } else {
      // Новых данных магнитометра нет — 6DOF, гироскоп+акселерометр
      if (madgwick_enabled_) {
        filter_.Update(raw_ax, raw_ay, raw_az, data_.gx, data_.gy, data_.gz,
                       dt_sec);
      }
    }
  } else {
    if (madgwick_enabled_) {
      filter_.Update(raw_ax, raw_ay, raw_az, data_.gx, data_.gy, data_.gz,
                     dt_sec);
    }
  }
}

float ImuHandler::GetRelativeHeadingDeg() const noexcept {
  float delta = heading_deg_ - heading_ref_;
  if (delta >  180.f) delta -= 360.f;
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

  if (platform_.GetWebSocketClientCount() == 0) {
    return;
  }

  std::string json = BuildTelemJson(snap);
  platform_.SendTelem(json);
}

std::string TelemetryHandler::BuildTelemJson(
    const TelemetrySnapshot& snap) const {
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
    cJSON_AddBoolToObject(link, "failsafe", platform_.FailsafeIsActive());
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