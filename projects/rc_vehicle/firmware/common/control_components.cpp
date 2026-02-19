#include "control_components.hpp"

#include <cstring>
#include <sstream>

#include "imu_calibration.hpp"
#include "madgwick_filter.hpp"

namespace rc_vehicle {

// ═════════════════════════════════════════════════════════════════════════
// RcInputHandler
// ═════════════════════════════════════════════════════════════════════════

void RcInputHandler::Update(uint32_t now_ms, uint32_t dt_ms) {
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

void WifiCommandHandler::Update(uint32_t now_ms, uint32_t dt_ms) {
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

void ImuHandler::Update(uint32_t now_ms, uint32_t dt_ms) {
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
  if (!lpf_gyro_z_.IsConfigured()) {
    const float fs_hz = 1000.f / static_cast<float>(read_interval_ms_);
    lpf_gyro_z_.SetParams(25.f, fs_hz);  // 25 Hz cutoff при 500 Hz
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

// ═════════════════════════════════════════════════════════════════════════
// TelemetryHandler
// ═════════════════════════════════════════════════════════════════════════

void TelemetryHandler::Update(uint32_t now_ms, uint32_t dt_ms) {
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
  std::ostringstream oss;
  oss << "{\"type\":\"telem\",";

  // Для совместимости: "mcu_pong_ok" = "контроллер жив"
  oss << "\"mcu_pong_ok\":true,";

  // Link status
  oss << "\"link\":{";
  oss << "\"rc_ok\":" << (rc_.IsActive() ? "true" : "false") << ",";
  oss << "\"wifi_ok\":" << (wifi_.IsActive() ? "true" : "false") << ",";
  oss << "\"failsafe\":" << (platform_.FailsafeIsActive() ? "true" : "false");
  oss << "},";

  // IMU data (если включен)
  if (imu_.IsEnabled()) {
    const auto& data = imu_.GetData();
    oss << "\"imu\":{";
    oss << "\"ax\":" << data.ax << ",";
    oss << "\"ay\":" << data.ay << ",";
    oss << "\"az\":" << data.az << ",";
    oss << "\"gx\":" << data.gx << ",";
    oss << "\"gy\":" << data.gy << ",";
    oss << "\"gz\":" << data.gz << ",";
    oss << "\"gyro_z_filtered\":" << imu_.GetFilteredGyroZ() << ",";
    oss << "\"forward_accel\":" << calib_.GetForwardAccel(data) << ",";

    // Orientation (Madgwick)
    float pitch_deg = 0.0f, roll_deg = 0.0f, yaw_deg = 0.0f;
    filter_.GetEulerDeg(pitch_deg, roll_deg, yaw_deg);
    oss << "\"orientation\":{";
    oss << "\"pitch\":" << pitch_deg << ",";
    oss << "\"roll\":" << roll_deg << ",";
    oss << "\"yaw\":" << yaw_deg;
    oss << "}";
    oss << "},";

    // Calibration status
    oss << "\"calib\":{";
    const char* status_str = "unknown";
    switch (calib_.GetStatus()) {
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
    oss << "\"status\":\"" << status_str << "\",";
    oss << "\"stage\":" << calib_.GetCalibStage() << ",";
    oss << "\"valid\":" << (calib_.IsValid() ? "true" : "false");

    if (calib_.IsValid()) {
      const auto& cd = calib_.GetData();
      oss << ",\"bias\":{";
      oss << "\"gx\":" << cd.gyro_bias[0] << ",";
      oss << "\"gy\":" << cd.gyro_bias[1] << ",";
      oss << "\"gz\":" << cd.gyro_bias[2] << ",";
      oss << "\"ax\":" << cd.accel_bias[0] << ",";
      oss << "\"ay\":" << cd.accel_bias[1] << ",";
      oss << "\"az\":" << cd.accel_bias[2];
      oss << "},";
      oss << "\"gravity_vec\":[" << cd.gravity_vec[0] << ","
          << cd.gravity_vec[1] << "," << cd.gravity_vec[2] << "],";
      oss << "\"forward_vec\":[" << cd.accel_forward_vec[0] << ","
          << cd.accel_forward_vec[1] << "," << cd.accel_forward_vec[2] << "]";
    }
    oss << "},";
  }

  // Actuators
  oss << "\"act\":{";
  oss << "\"throttle\":" << applied_throttle_ << ",";
  oss << "\"steering\":" << applied_steering_;
  oss << "}";

  oss << "}";
  return oss.str();
}

}  // namespace rc_vehicle