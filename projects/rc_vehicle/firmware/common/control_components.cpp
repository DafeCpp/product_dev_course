#include "control_components.hpp"

#include <cstdlib>
#include <cstring>
#include <string>

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
  // Активность проверяется через IsActive() - команда актуальна в пределах
  // timeout
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

  // Применить компенсацию bias (если калибровка валидна)
  calib_.Apply(data_);

  // LPF Butterworth 2-го порядка для gyro Z (подготовка к yaw rate PID)
  // Инициализация с дефолтными параметрами, если ещё не настроен
  if (!lpf_gyro_z_.IsConfigured()) {
    const float fs_hz = 1000.f / static_cast<float>(read_interval_ms_);
    lpf_gyro_z_.SetParams(config::LpfConfig::kDefaultCutoffHz, fs_hz);
  }
  filtered_gz_ = lpf_gyro_z_.Step(data_.gz);

  // Настроить опорную СК фильтра
  if (calib_.IsValid()) {
    const auto& calib_data = calib_.GetData();
    filter_.SetVehicleFrame(calib_data.gravity_vec,
                            calib_data.accel_forward_vec, true);
  } else {
    filter_.SetVehicleFrame(nullptr, nullptr, false);
  }

  // Обновить фильтр Madgwick
  const float dt_sec = (prev_read_ms == 0)
                           ? (read_interval_ms_ / 1000.0f)
                           : ((now_ms - prev_read_ms) / 1000.0f);
  filter_.Update(data_, dt_sec);
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

void TelemetryHandler::Update(uint32_t now_ms, const TelemetrySnapshot& snap) {
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

    // EKF: динамическое состояние (vx, vy, r, slip angle)
    if (snap.ekf_available) {
      cJSON* ekf = cJSON_AddObjectToObject(root, "ekf");
      if (ekf) {
        cJSON_AddNumberToObject(ekf, "vx", snap.ekf_vx);
        cJSON_AddNumberToObject(ekf, "vy", snap.ekf_vy);
        cJSON_AddNumberToObject(ekf, "yaw_rate", snap.ekf_yaw_rate);
        cJSON_AddNumberToObject(ekf, "slip_deg", snap.ekf_slip_deg);
        cJSON_AddNumberToObject(ekf, "speed_ms", snap.ekf_speed_ms);
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

  // Actuators
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