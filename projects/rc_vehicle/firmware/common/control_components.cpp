#include "control_components.hpp"

#include <cstdlib>
#include <cstring>
#include <string>

#include "cJSON.h"
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

void WifiCommandHandler::Update(uint32_t now_ms, [[maybe_unused]] uint32_t dt_ms) {
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
    lpf_gyro_z_.SetParams(30.f, fs_hz);  // 30 Hz cutoff по умолчанию
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
  if (cutoff_hz < 5.f || cutoff_hz > 100.f) {
    return;  // Игнорировать невалидные значения
  }
  const float fs_hz = 1000.f / static_cast<float>(read_interval_ms_);
  lpf_gyro_z_.SetParams(cutoff_hz, fs_hz);
  lpf_gyro_z_.Reset();  // Сброс состояния при изменении параметров
}

// ═════════════════════════════════════════════════════════════════════════
// TelemetryHandler
// ═════════════════════════════════════════════════════════════════════════

void TelemetryHandler::Update(uint32_t now_ms, [[maybe_unused]] uint32_t dt_ms) {
  // Отправка телеметрии с заданной частотой
  if (now_ms - last_send_ms_ < send_interval_ms_) {
    return;
  }
  last_send_ms_ = now_ms;

  // Если клиентов нет - не отправляем
  if (platform_.GetWebSocketClientCount() == 0) {
    return;
  }

  // Построить и отправить JSON
  std::string json = BuildTelemJson();
  platform_.SendTelem(json);
}

std::string TelemetryHandler::BuildTelemJson() const {
  cJSON* root = cJSON_CreateObject();
  if (!root) return "{}";

  cJSON_AddStringToObject(root, "type", "telem");
  // Для совместимости: "mcu_pong_ok" = "контроллер жив"
  cJSON_AddBoolToObject(root, "mcu_pong_ok", true);

  // Link status
  cJSON* link = cJSON_AddObjectToObject(root, "link");
  if (link) {
    cJSON_AddBoolToObject(link, "rc_ok", rc_.IsActive());
    cJSON_AddBoolToObject(link, "wifi_ok", wifi_.IsActive());
    cJSON_AddBoolToObject(link, "failsafe", platform_.FailsafeIsActive());
  }

  // IMU data (если включен)
  if (imu_.IsEnabled()) {
    const auto& data = imu_.GetData();

    cJSON* imu = cJSON_AddObjectToObject(root, "imu");
    if (imu) {
      cJSON_AddNumberToObject(imu, "ax", data.ax);
      cJSON_AddNumberToObject(imu, "ay", data.ay);
      cJSON_AddNumberToObject(imu, "az", data.az);
      cJSON_AddNumberToObject(imu, "gx", data.gx);
      cJSON_AddNumberToObject(imu, "gy", data.gy);
      cJSON_AddNumberToObject(imu, "gz", data.gz);
      cJSON_AddNumberToObject(imu, "gyro_z_filtered", imu_.GetFilteredGyroZ());
      cJSON_AddNumberToObject(imu, "forward_accel", calib_.GetForwardAccel(data));

      // Orientation (Madgwick)
      float pitch_deg = 0.0f, roll_deg = 0.0f, yaw_deg = 0.0f;
      filter_.GetEulerDeg(pitch_deg, roll_deg, yaw_deg);
      cJSON* orientation = cJSON_AddObjectToObject(imu, "orientation");
      if (orientation) {
        cJSON_AddNumberToObject(orientation, "pitch", pitch_deg);
        cJSON_AddNumberToObject(orientation, "roll", roll_deg);
        cJSON_AddNumberToObject(orientation, "yaw", yaw_deg);
      }
    }

    // Calibration status
    cJSON* calib = cJSON_AddObjectToObject(root, "calib");
    if (calib) {
      const char* status_str = "unknown";
      switch (calib_.GetStatus()) {
        case CalibStatus::Idle:       status_str = "idle";       break;
        case CalibStatus::Collecting: status_str = "collecting"; break;
        case CalibStatus::Done:       status_str = "done";       break;
        case CalibStatus::Failed:     status_str = "failed";     break;
      }
      cJSON_AddStringToObject(calib, "status", status_str);
      cJSON_AddNumberToObject(calib, "stage", calib_.GetCalibStage());
      cJSON_AddBoolToObject(calib, "valid", calib_.IsValid());

      if (calib_.IsValid()) {
        const auto& cd = calib_.GetData();
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
    if (ekf_) {
      cJSON* ekf = cJSON_AddObjectToObject(root, "ekf");
      if (ekf) {
        cJSON_AddNumberToObject(ekf, "vx", ekf_->GetVx());
        cJSON_AddNumberToObject(ekf, "vy", ekf_->GetVy());
        cJSON_AddNumberToObject(ekf, "yaw_rate", ekf_->GetYawRate());
        cJSON_AddNumberToObject(ekf, "slip_deg", ekf_->GetSlipAngleDeg());
        cJSON_AddNumberToObject(ekf, "speed_ms", ekf_->GetSpeedMs());
      }
    }

    // Oversteer warning (Phase 4.2)
    if (oversteer_warn_ptr_) {
      cJSON* warn = cJSON_AddObjectToObject(root, "warn");
      if (warn) {
        cJSON_AddBoolToObject(warn, "oversteer", *oversteer_warn_ptr_);
      }
    }
  }

  // Actuators
  cJSON* act = cJSON_AddObjectToObject(root, "act");
  if (act) {
    cJSON_AddNumberToObject(act, "throttle", applied_throttle_);
    cJSON_AddNumberToObject(act, "steering", applied_steering_);
  }

  char* str = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!str) return "{}";
  std::string result(str);
  free(str);
  return result;
}

}  // namespace rc_vehicle