#include "vehicle_control.hpp"

#include <stdlib.h>

#include "cJSON.h"
#include "config.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "failsafe.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "imu.hpp"
#include "imu_calibration.hpp"
#include "imu_calibration_nvs.hpp"
#include "madgwick_filter.hpp"
#include "pwm_control.hpp"
#include "rc_input.hpp"
#include "rc_vehicle_common.hpp"
#include "slew_rate.hpp"
#include "websocket_server.hpp"

static const char* TAG = "vehicle_control";

static constexpr uint32_t CONTROL_TASK_STACK = 12288;
static constexpr UBaseType_t CONTROL_TASK_PRIORITY = configMAX_PRIORITIES - 1;

struct WifiCmd {
  float throttle{0.0f};
  float steering{0.0f};
};

static uint32_t NowMs() { return (uint32_t)(esp_timer_get_time() / 1000); }

VehicleControl& VehicleControl::Instance() {
  static VehicleControl s_instance;
  return s_instance;
}

void VehicleControl::ControlTaskEntry(void* arg) {
  auto* self = static_cast<VehicleControl*>(arg);
  if (self) self->ControlTaskLoop();
}

void VehicleControl::ControlTaskLoop() {
  // commanded_* — что хотим (RC/Wi‑Fi), applied_* — что реально подаём на PWM
  // (slew-rate)
  float commanded_throttle = 0.0f;
  float commanded_steering = 0.0f;
  float applied_throttle = 0.0f;
  float applied_steering = 0.0f;

  bool rc_active = false;
  bool wifi_active = false;

  uint32_t last_pwm_update = NowMs();
  uint32_t last_rc_poll = NowMs();
  uint32_t last_imu_read = NowMs();
  uint32_t last_telem_send = NowMs();
  uint32_t last_failsafe_update = NowMs();
  uint32_t last_wifi_cmd_ms = 0;

  ImuData imu_data = {0};

  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xPeriod = pdMS_TO_TICKS(CONTROL_LOOP_PERIOD_MS);

  // Диагностика: счётчики для замера реальной частоты
  uint32_t diag_loop_count = 0;
  uint32_t diag_imu_count = 0;
  uint32_t diag_start_ms = NowMs();
  uint32_t diag_max_dt_us = 0;

  while (1) {
    const uint64_t iter_start_us = esp_timer_get_time();
    vTaskDelayUntil(&xLastWakeTime, xPeriod ? xPeriod : 1);
    const uint32_t now = NowMs();
    ++diag_loop_count;

    // Опрос RC-in (50 Hz)
    if (rc_enabled_ && (now - last_rc_poll >= RC_IN_POLL_INTERVAL_MS)) {
      last_rc_poll = now;

      auto rc_throttle = RcInputReadThrottle();
      auto rc_steering = RcInputReadSteering();
      rc_active = rc_throttle.has_value() && rc_steering.has_value();

      // RC имеет приоритет над Wi-Fi
      if (rc_active) {
        commanded_throttle = *rc_throttle;
        commanded_steering = *rc_steering;
      }
    } else if (!rc_enabled_) {
      rc_active = false;
    }

    // Чтение команд от Wi‑Fi (WebSocket)
    WifiCmd cmd;
    if (cmd_queue_ && xQueueReceive(cmd_queue_, &cmd, 0) == pdTRUE) {
      // Wi‑Fi команды принимаются только если RC не активен
      if (!rc_active) {
        commanded_throttle = cmd.throttle;
        commanded_steering = cmd.steering;
        last_wifi_cmd_ms = now;
      }
    }

    // Wi‑Fi активен, если команда приходила недавно и RC не активен
    wifi_active = (!rc_active) && (last_wifi_cmd_ms != 0) &&
                  ((now - last_wifi_cmd_ms) < WIFI_CMD_TIMEOUT_MS);

    // Запрос на калибровку этапа 1 (от WebSocket; этап 2 запускается напрямую
    // из main)
    {
      int req = calib_request_;
      if (req != 0) {
        calib_request_ = 0;
        CalibMode mode = (req == 2) ? CalibMode::Full : CalibMode::GyroOnly;
        int samples = (req == 2) ? 2000 : 1000;
        imu_calib_.StartCalibration(mode, samples);
        ESP_LOGI(TAG, "Calibration stage 1 started (mode=%d, samples=%d)", req,
                 samples);
      }
    }

    // Чтение IMU (500 Hz — каждую итерацию control loop)
    if (imu_enabled_ && (now - last_imu_read >= IMU_READ_INTERVAL_MS)) {
      last_imu_read = now;
      if (ImuRead(imu_data) == 0) {
        ++diag_imu_count;

        // Подача семпла в калибровку (если идёт сбор)
        if (imu_calib_.GetStatus() == CalibStatus::Collecting) {
          imu_calib_.FeedSample(imu_data);

          // Проверяем завершение калибровки
          CalibStatus st = imu_calib_.GetStatus();
          if (st == CalibStatus::Done) {
            const auto& d = imu_calib_.GetData();
            ESP_LOGI(TAG,
                     "IMU calibration DONE — gyro bias: [%.3f, %.3f, %.3f] "
                     "accel bias: [%.4f, %.4f, %.4f] gravity_vec: [%.3f, %.3f, "
                     "%.3f] forward_vec: [%.3f, %.3f, %.3f]",
                     d.gyro_bias[0], d.gyro_bias[1], d.gyro_bias[2],
                     d.accel_bias[0], d.accel_bias[1], d.accel_bias[2],
                     d.gravity_vec[0], d.gravity_vec[1], d.gravity_vec[2],
                     d.accel_forward_vec[0], d.accel_forward_vec[1],
                     d.accel_forward_vec[2]);
            if (imu_nvs::Save(imu_calib_.GetData()) == ESP_OK) {
              ESP_LOGI(TAG, "Calibration saved to NVS");
            }
          } else if (st == CalibStatus::Failed) {
            ESP_LOGW(TAG,
                     "IMU calibration FAILED (motion detected or insufficient "
                     "data)");
            if (imu_calib_.IsValid()) {
              ESP_LOGI(TAG, "Using previously saved calibration data");
            }
          }
        }

        // Применяем компенсацию bias (если калибровка валидна)
        imu_calib_.Apply(imu_data);

        // Опорная СК фильтра: по умолчанию NED; при валидной калибровке — СК
        // машины (g + «вперёд»)
        if (imu_calib_.IsValid()) {
          const auto& d = imu_calib_.GetData();
          madgwick_.SetVehicleFrame(d.gravity_vec, d.accel_forward_vec, true);
        } else {
          madgwick_.SetVehicleFrame(nullptr, nullptr, false);
        }

        // Обновление фильтра Madgwick (ориентация по gyro + accel)
        const float dt_sec = IMU_READ_INTERVAL_MS / 1000.f;
        madgwick_.Update(imu_data, dt_sec);
      }
    }

    // Диагностика: вывод реальной частоты каждые 5 секунд
    {
      const uint32_t elapsed = now - diag_start_ms;
      if (elapsed >= 5000) {
        const uint32_t loop_hz = diag_loop_count * 1000 / elapsed;
        const uint32_t imu_hz = diag_imu_count * 1000 / elapsed;
        ESP_LOGI(TAG,
                 "DIAG: loop=%lu Hz, imu=%lu Hz, max_dt=%lu us, "
                 "iters=%lu in %lu ms",
                 (unsigned long)loop_hz, (unsigned long)imu_hz,
                 (unsigned long)diag_max_dt_us, (unsigned long)diag_loop_count,
                 (unsigned long)elapsed);

        // Информация о калибровке IMU
        const char* calib_str = "off";
        switch (imu_calib_.GetStatus()) {
          case CalibStatus::Idle:
            calib_str = "idle";
            break;
          case CalibStatus::Collecting:
            calib_str = "collecting";
            break;
          case CalibStatus::Done:
            calib_str = "done";
            break;
          case CalibStatus::Failed:
            calib_str = "failed";
            break;
        }
        if (imu_calib_.IsValid()) {
          const auto& d = imu_calib_.GetData();
          ESP_LOGI(TAG,
                   "CALIB: status=%s valid=YES gyro_bias=[%.3f, %.3f, %.3f] "
                   "accel_bias=[%.4f, %.4f, %.4f]",
                   calib_str, d.gyro_bias[0], d.gyro_bias[1], d.gyro_bias[2],
                   d.accel_bias[0], d.accel_bias[1], d.accel_bias[2]);
        } else {
          ESP_LOGI(TAG, "CALIB: status=%s valid=NO", calib_str);
        }

        // Вывод текущих (откалиброванных) значений IMU и ориентации Madgwick
        if (imu_enabled_) {
          float pitch_deg = 0.f, roll_deg = 0.f, yaw_deg = 0.f;
          madgwick_.GetEulerDeg(pitch_deg, roll_deg, yaw_deg);
          ESP_LOGI(TAG,
                   "IMU: ax=%.3f ay=%.3f az=%.3f gx=%.2f gy=%.2f gz=%.2f | "
                   "orientation pitch=%.1f roll=%.1f yaw=%.1f deg",
                   imu_data.ax, imu_data.ay, imu_data.az, imu_data.gx,
                   imu_data.gy, imu_data.gz, pitch_deg, roll_deg, yaw_deg);
        }

        diag_loop_count = 0;
        diag_imu_count = 0;
        diag_max_dt_us = 0;
        diag_start_ms = now;
      }
    }

    // Обновление failsafe
    if (now - last_failsafe_update >= 10) {  // Каждые 10 мс
      last_failsafe_update = now;
      if (FailsafeUpdate(rc_active, wifi_active)) {
        // Failsafe активен: нейтраль
        commanded_throttle = 0.0f;
        commanded_steering = 0.0f;
        applied_throttle = 0.0f;
        applied_steering = 0.0f;
        PwmControlSetNeutral();
      }
    }

    // Обновление PWM (50 Hz)
    if (now - last_pwm_update >= PWM_UPDATE_INTERVAL_MS) {
      const uint32_t dt_ms = now - last_pwm_update;
      last_pwm_update = now;

      applied_throttle = ApplySlewRate(commanded_throttle, applied_throttle,
                                       SLEW_RATE_THROTTLE_MAX_PER_SEC, dt_ms);
      applied_steering = ApplySlewRate(commanded_steering, applied_steering,
                                       SLEW_RATE_STEERING_MAX_PER_SEC, dt_ms);

      (void)PwmControlSetThrottle(applied_throttle);
      (void)PwmControlSetSteering(applied_steering);
    }

    // Отправка телеметрии (20 Hz)
    if (now - last_telem_send >= TELEM_SEND_INTERVAL_MS) {
      last_telem_send = now;

      // Если клиентов нет — не аллоцируем JSON зря.
      if (WebSocketGetClientCount() == 0) {
        continue;
      }

      cJSON* root = cJSON_CreateObject();
      if (root) {
        cJSON_AddStringToObject(root, "type", "telem");

        // Для совместимости с текущим UI: “mcu_pong_ok” = “контроллер жив”.
        cJSON_AddBoolToObject(root, "mcu_pong_ok", true);

        cJSON* link = cJSON_CreateObject();
        if (link) {
          cJSON_AddBoolToObject(link, "rc_ok", rc_active);
          cJSON_AddBoolToObject(link, "wifi_ok", wifi_active);
          cJSON_AddBoolToObject(link, "failsafe", FailsafeIsActive());
          cJSON_AddItemToObject(root, "link", link);
        }

        if (imu_enabled_) {
          cJSON* imu = cJSON_CreateObject();
          if (imu) {
            cJSON_AddNumberToObject(imu, "ax", imu_data.ax);
            cJSON_AddNumberToObject(imu, "ay", imu_data.ay);
            cJSON_AddNumberToObject(imu, "az", imu_data.az);
            cJSON_AddNumberToObject(imu, "gx", imu_data.gx);
            cJSON_AddNumberToObject(imu, "gy", imu_data.gy);
            cJSON_AddNumberToObject(imu, "gz", imu_data.gz);
            cJSON_AddNumberToObject(imu, "forward_accel",
                                    imu_calib_.GetForwardAccel(imu_data));
            float pitch_deg = 0.f, roll_deg = 0.f, yaw_deg = 0.f;
            madgwick_.GetEulerDeg(pitch_deg, roll_deg, yaw_deg);
            cJSON* orient = cJSON_CreateObject();
            if (orient) {
              cJSON_AddNumberToObject(orient, "pitch", pitch_deg);
              cJSON_AddNumberToObject(orient, "roll", roll_deg);
              cJSON_AddNumberToObject(orient, "yaw", yaw_deg);
              cJSON_AddItemToObject(imu, "orientation", orient);
            }
            cJSON_AddItemToObject(root, "imu", imu);
          }

          // Статус калибровки IMU
          cJSON* calib = cJSON_CreateObject();
          if (calib) {
            const char* status_str = "unknown";
            switch (imu_calib_.GetStatus()) {
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
            cJSON_AddNumberToObject(calib, "stage", GetCalibStage());
            cJSON_AddBoolToObject(calib, "valid", imu_calib_.IsValid());
            if (imu_calib_.IsValid()) {
              const auto& d = imu_calib_.GetData();
              cJSON* bias = cJSON_CreateObject();
              if (bias) {
                cJSON_AddNumberToObject(bias, "gx", d.gyro_bias[0]);
                cJSON_AddNumberToObject(bias, "gy", d.gyro_bias[1]);
                cJSON_AddNumberToObject(bias, "gz", d.gyro_bias[2]);
                cJSON_AddNumberToObject(bias, "ax", d.accel_bias[0]);
                cJSON_AddNumberToObject(bias, "ay", d.accel_bias[1]);
                cJSON_AddNumberToObject(bias, "az", d.accel_bias[2]);
                cJSON_AddItemToObject(calib, "bias", bias);
              }
              cJSON* gvec = cJSON_CreateArray();
              if (gvec) {
                cJSON_AddItemToArray(gvec,
                                     cJSON_CreateNumber(d.gravity_vec[0]));
                cJSON_AddItemToArray(gvec,
                                     cJSON_CreateNumber(d.gravity_vec[1]));
                cJSON_AddItemToArray(gvec,
                                     cJSON_CreateNumber(d.gravity_vec[2]));
                cJSON_AddItemToObject(calib, "gravity_vec", gvec);
              }
              cJSON* fvec = cJSON_CreateArray();
              if (fvec) {
                cJSON_AddItemToArray(
                    fvec, cJSON_CreateNumber(d.accel_forward_vec[0]));
                cJSON_AddItemToArray(
                    fvec, cJSON_CreateNumber(d.accel_forward_vec[1]));
                cJSON_AddItemToArray(
                    fvec, cJSON_CreateNumber(d.accel_forward_vec[2]));
                cJSON_AddItemToObject(calib, "forward_vec", fvec);
              }
            }
            cJSON_AddItemToObject(root, "calib", calib);
          }
        }

        cJSON* act = cJSON_CreateObject();
        if (act) {
          cJSON_AddNumberToObject(act, "throttle", applied_throttle);
          cJSON_AddNumberToObject(act, "steering", applied_steering);
          cJSON_AddItemToObject(root, "act", act);
        }

        char* json_str = cJSON_PrintUnformatted(root);
        if (json_str) {
          (void)WebSocketSendTelem(json_str);
          free(json_str);
        }
        cJSON_Delete(root);
      }
    }

    // Замер длительности итерации (jitter)
    const uint32_t iter_dt_us =
        (uint32_t)(esp_timer_get_time() - iter_start_us);
    if (iter_dt_us > diag_max_dt_us) diag_max_dt_us = iter_dt_us;
  }
}

esp_err_t VehicleControl::Init() {
  if (inited_) return ESP_OK;

  if (PwmControlInit() != 0) {
    ESP_LOGE(TAG, "Failed to initialize PWM");
    return ESP_FAIL;
  }

  if (RcInputInit() == 0) {
    rc_enabled_ = true;
  } else {
    rc_enabled_ = false;
    ESP_LOGW(TAG, "RC input init failed — continuing without RC-in");
  }

  if (ImuInit() == 0) {
    imu_enabled_ = true;
    ImuCalibData nvs_data{};
    if (imu_nvs::Load(nvs_data) == ESP_OK) {
      imu_calib_.SetData(nvs_data);
      if (imu_calib_.IsValid()) {
        const auto& d = imu_calib_.GetData();
        madgwick_.SetVehicleFrame(d.gravity_vec, d.accel_forward_vec, true);
      }
      ESP_LOGI(TAG, "IMU calibration loaded from NVS (valid=%d)",
               nvs_data.valid);
    } else {
      ESP_LOGI(TAG, "No saved IMU calibration — will auto-calibrate at start");
    }
    imu_calib_.StartCalibration(CalibMode::Full, 1000);
    ESP_LOGI(TAG,
             "IMU auto-calibration started (Full: gyro + accel, 1000 samples)");
  } else {
    imu_enabled_ = false;
    const int who = ImuGetLastWhoAmI();
    ESP_LOGW(TAG, "IMU init failed — continuing without IMU");
    if (who >= 0) {
      ESP_LOGW(TAG,
               "IMU WHO_AM_I=0x%02X (expected 0x68 MPU-6050 or 0x70 MPU-6500)",
               who);
    } else {
      ESP_LOGW(TAG,
               "IMU SPI read failed — check wiring: CS=%d, SCK=%d, MOSI=%d, "
               "MISO=%d, 3V3/GND",
               (int)IMU_SPI_CS_PIN, (int)IMU_SPI_SCK_PIN, (int)IMU_SPI_MOSI_PIN,
               (int)IMU_SPI_MISO_PIN);
    }
  }

  FailsafeInit();

  cmd_queue_ = xQueueCreate(1, sizeof(WifiCmd));
  if (cmd_queue_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create Wi-Fi command queue");
    return ESP_FAIL;
  }

  BaseType_t created = xTaskCreatePinnedToCore(ControlTaskEntry, "vehicle_ctrl",
                                               CONTROL_TASK_STACK, this,
                                               CONTROL_TASK_PRIORITY, NULL, 1);
  if (created != pdPASS) {
    ESP_LOGE(TAG, "Failed to create vehicle control task");
    return ESP_FAIL;
  }

  inited_ = true;
  ESP_LOGI(TAG, "Vehicle control started");
  return ESP_OK;
}

void VehicleControl::OnWifiCommand(float throttle, float steering) {
  if (cmd_queue_ == nullptr) return;
  WifiCmd cmd = {
      .throttle = rc_vehicle::ClampNormalized(throttle),
      .steering = rc_vehicle::ClampNormalized(steering),
  };
  (void)xQueueOverwrite(cmd_queue_, &cmd);
}

void VehicleControl::StartCalibration(bool full) {
  calib_request_ = full ? 2 : 1;
}

bool VehicleControl::StartForwardCalibration() {
  return imu_calib_.StartForwardCalibration(2000);
}

const char* VehicleControl::GetCalibStatus() const {
  switch (imu_calib_.GetStatus()) {
    case CalibStatus::Idle:
      return "idle";
    case CalibStatus::Collecting:
      return "collecting";
    case CalibStatus::Done:
      return "done";
    case CalibStatus::Failed:
      return "failed";
  }
  return "unknown";
}

int VehicleControl::GetCalibStage() const { return imu_calib_.GetCalibStage(); }

void VehicleControl::SetForwardDirection(float fx, float fy, float fz) {
  imu_calib_.SetForwardDirection(fx, fy, fz);
  if (imu_nvs::Save(imu_calib_.GetData()) == ESP_OK) {
    const auto& v = imu_calib_.GetData().accel_forward_vec;
    ESP_LOGI(TAG, "Forward direction set: vec=[%.3f, %.3f, %.3f], saved to NVS",
             v[0], v[1], v[2]);
  }
}
