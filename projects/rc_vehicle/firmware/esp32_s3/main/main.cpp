#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "config.hpp"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_server.hpp"
#include "vehicle_control.hpp"
#include "websocket_server.hpp"
#include "wifi_ap.hpp"

static const char* TAG = "main";

static void ws_cmd_handler(float throttle, float steering) {
  VehicleControlOnWifiCommand(throttle, steering);
}

/** Отправить JSON-ответ обратно в тот же WebSocket-фрейм. */
static void ws_send_reply(httpd_req_t* req, cJSON* reply) {
  char* str = cJSON_PrintUnformatted(reply);
  if (str) {
    httpd_ws_frame_t pkt = {};
    pkt.final = true;
    pkt.fragmented = false;
    pkt.type = HTTPD_WS_TYPE_TEXT;
    pkt.payload = reinterpret_cast<uint8_t*>(str);
    pkt.len = strlen(str);
    httpd_ws_send_frame(req, &pkt);
    free(str);
  }
}

/**
 * Обработчик произвольных JSON-команд через WebSocket.
 *
 * Протокол:
 *   → {"type":"calibrate_imu","mode":"gyro"}    — этап 1: только гироскоп
 *   → {"type":"calibrate_imu","mode":"full"}    — этап 1: стояние на месте
 * (bias + вектор g) → {"type":"calibrate_imu","mode":"forward"} — этап 2:
 * движение вперёд/назад с прямыми колёсами ←
 * {"type":"calibrate_imu_ack","status":"collecting","stage":1|2,"ok":true|false}
 *
 *   → {"type":"get_calib_status"}
 *   ← {"type":"calib_status","status":"done","valid":true}
 *
 *   → {"type":"set_forward_direction","vec":[fx,fy,fz]}  — единичный вектор
 * «вперёд» в СК датчика (нормализуется) ←
 * {"type":"set_forward_direction_ack","ok":true}
 *
 *   → {"type":"get_stab_config"}
 *   ←
 * {"type":"stab_config","enabled":false,"madgwick_beta":0.1,"lpf_cutoff_hz":30.0,"mode":0}
 *
 *   →
 * {"type":"set_stab_config","enabled":true,"madgwick_beta":0.15,"lpf_cutoff_hz":25.0}
 *   ← {"type":"set_stab_config_ack","ok":true}
 */
static void ws_json_handler(const char* type, cJSON* json, httpd_req_t* req) {
  if (strcmp(type, "calibrate_imu") == 0) {
    cJSON* mode_item = cJSON_GetObjectItem(json, "mode");
    const char* mode_str = (mode_item && cJSON_IsString(mode_item))
                               ? mode_item->valuestring
                               : "gyro";
    bool is_forward = (strcmp(mode_str, "forward") == 0);
    bool full = (strcmp(mode_str, "full") == 0);

    cJSON* reply = cJSON_CreateObject();
    if (reply) {
      cJSON_AddStringToObject(reply, "type", "calibrate_imu_ack");
      if (is_forward) {
        bool ok = VehicleControlStartForwardCalibration();
        cJSON_AddStringToObject(reply, "status", ok ? "collecting" : "failed");
        cJSON_AddNumberToObject(reply, "stage", 2);
        cJSON_AddBoolToObject(reply, "ok", ok);
        ESP_LOGI(TAG, "WS: calibrate_imu mode=forward -> %s",
                 ok ? "stage 2 started" : "failed (need stage 1 full)");
      } else {
        VehicleControlStartCalibration(full);
        cJSON_AddStringToObject(reply, "status", "collecting");
        cJSON_AddNumberToObject(reply, "stage", 1);
        cJSON_AddBoolToObject(reply, "ok", true);
        cJSON_AddStringToObject(reply, "mode", full ? "full" : "gyro");
        ESP_LOGI(TAG, "WS: calibrate_imu (stage 1, mode=%s)",
                 full ? "full" : "gyro");
      }
      ws_send_reply(req, reply);
      cJSON_Delete(reply);
    }
  } else if (strcmp(type, "get_calib_status") == 0) {
    cJSON* reply = cJSON_CreateObject();
    if (reply) {
      cJSON_AddStringToObject(reply, "type", "calib_status");
      cJSON_AddStringToObject(reply, "status", VehicleControlGetCalibStatus());
      cJSON_AddNumberToObject(reply, "stage", VehicleControlGetCalibStage());
      ws_send_reply(req, reply);
      cJSON_Delete(reply);
    }
  } else if (strcmp(type, "set_forward_direction") == 0) {
    cJSON* vec_arr = cJSON_GetObjectItem(json, "vec");
    float fx = 1.f, fy = 0.f, fz = 0.f;
    if (cJSON_IsArray(vec_arr) && cJSON_GetArraySize(vec_arr) >= 3) {
      cJSON* ex = cJSON_GetArrayItem(vec_arr, 0);
      cJSON* ey = cJSON_GetArrayItem(vec_arr, 1);
      cJSON* ez = cJSON_GetArrayItem(vec_arr, 2);
      if (cJSON_IsNumber(ex)) fx = (float)ex->valuedouble;
      if (cJSON_IsNumber(ey)) fy = (float)ey->valuedouble;
      if (cJSON_IsNumber(ez)) fz = (float)ez->valuedouble;
    }
    VehicleControlSetForwardDirection(fx, fy, fz);
    cJSON* reply = cJSON_CreateObject();
    if (reply) {
      cJSON_AddStringToObject(reply, "type", "set_forward_direction_ack");
      cJSON_AddBoolToObject(reply, "ok", true);
      ws_send_reply(req, reply);
      cJSON_Delete(reply);
    }
  } else if (strcmp(type, "get_stab_config") == 0) {
    const auto& cfg = VehicleControlGetStabilizationConfig();
    cJSON* reply = cJSON_CreateObject();
    if (reply) {
      cJSON_AddStringToObject(reply, "type", "stab_config");
      cJSON_AddBoolToObject(reply, "enabled", cfg.enabled);
      cJSON_AddNumberToObject(reply, "madgwick_beta", cfg.madgwick_beta);
      cJSON_AddNumberToObject(reply, "lpf_cutoff_hz", cfg.lpf_cutoff_hz);
      cJSON_AddNumberToObject(reply, "imu_sample_rate_hz",
                              cfg.imu_sample_rate_hz);
      cJSON_AddNumberToObject(reply, "mode", cfg.mode);
      // PID-коэффициенты
      cJSON_AddNumberToObject(reply, "pid_kp", cfg.pid_kp);
      cJSON_AddNumberToObject(reply, "pid_ki", cfg.pid_ki);
      cJSON_AddNumberToObject(reply, "pid_kd", cfg.pid_kd);
      cJSON_AddNumberToObject(reply, "pid_max_integral", cfg.pid_max_integral);
      cJSON_AddNumberToObject(reply, "pid_max_correction",
                              cfg.pid_max_correction);
      cJSON_AddNumberToObject(reply, "steer_to_yaw_rate_dps",
                              cfg.steer_to_yaw_rate_dps);
      cJSON_AddNumberToObject(reply, "fade_ms", cfg.fade_ms);
      // Pitch compensation (slope stabilization)
      cJSON_AddBoolToObject(reply, "pitch_comp_enabled", cfg.pitch_comp_enabled);
      cJSON_AddNumberToObject(reply, "pitch_comp_gain", cfg.pitch_comp_gain);
      cJSON_AddNumberToObject(reply, "pitch_comp_max_correction",
                              cfg.pitch_comp_max_correction);
      ws_send_reply(req, reply);
      cJSON_Delete(reply);
    }
  } else if (strcmp(type, "set_stab_config") == 0) {
    StabilizationConfig cfg = VehicleControlGetStabilizationConfig();

    // Обновить параметры из JSON (если указаны)
    cJSON* enabled = cJSON_GetObjectItem(json, "enabled");
    if (enabled && cJSON_IsBool(enabled)) cfg.enabled = cJSON_IsTrue(enabled);

    cJSON* beta = cJSON_GetObjectItem(json, "madgwick_beta");
    if (beta && cJSON_IsNumber(beta)) cfg.madgwick_beta = (float)beta->valuedouble;

    cJSON* cutoff = cJSON_GetObjectItem(json, "lpf_cutoff_hz");
    if (cutoff && cJSON_IsNumber(cutoff)) cfg.lpf_cutoff_hz = (float)cutoff->valuedouble;

    cJSON* mode = cJSON_GetObjectItem(json, "mode");
    if (mode && cJSON_IsNumber(mode)) cfg.mode = (uint8_t)mode->valueint;

    // PID-коэффициенты (опциональные)
    cJSON* kp = cJSON_GetObjectItem(json, "pid_kp");
    if (kp && cJSON_IsNumber(kp)) cfg.pid_kp = (float)kp->valuedouble;

    cJSON* ki = cJSON_GetObjectItem(json, "pid_ki");
    if (ki && cJSON_IsNumber(ki)) cfg.pid_ki = (float)ki->valuedouble;

    cJSON* kd = cJSON_GetObjectItem(json, "pid_kd");
    if (kd && cJSON_IsNumber(kd)) cfg.pid_kd = (float)kd->valuedouble;

    cJSON* max_integral = cJSON_GetObjectItem(json, "pid_max_integral");
    if (max_integral && cJSON_IsNumber(max_integral))
      cfg.pid_max_integral = (float)max_integral->valuedouble;

    cJSON* max_corr = cJSON_GetObjectItem(json, "pid_max_correction");
    if (max_corr && cJSON_IsNumber(max_corr))
      cfg.pid_max_correction = (float)max_corr->valuedouble;

    cJSON* steer_dps = cJSON_GetObjectItem(json, "steer_to_yaw_rate_dps");
    if (steer_dps && cJSON_IsNumber(steer_dps))
      cfg.steer_to_yaw_rate_dps = (float)steer_dps->valuedouble;

    cJSON* fade = cJSON_GetObjectItem(json, "fade_ms");
    if (fade && cJSON_IsNumber(fade))
      cfg.fade_ms = (uint32_t)fade->valueint;

    // Pitch compensation (slope stabilization)
    cJSON* pitch_en = cJSON_GetObjectItem(json, "pitch_comp_enabled");
    if (pitch_en && cJSON_IsBool(pitch_en))
      cfg.pitch_comp_enabled = cJSON_IsTrue(pitch_en);

    cJSON* pitch_gain = cJSON_GetObjectItem(json, "pitch_comp_gain");
    if (pitch_gain && cJSON_IsNumber(pitch_gain))
      cfg.pitch_comp_gain = (float)pitch_gain->valuedouble;

    cJSON* pitch_max = cJSON_GetObjectItem(json, "pitch_comp_max_correction");
    if (pitch_max && cJSON_IsNumber(pitch_max))
      cfg.pitch_comp_max_correction = (float)pitch_max->valuedouble;

    bool ok = VehicleControlSetStabilizationConfig(cfg, true);

    // Получить применённую конфигурацию (могут применяться mode defaults)
    const auto& applied = VehicleControlGetStabilizationConfig();
    cJSON* reply = cJSON_CreateObject();
    if (reply) {
      cJSON_AddStringToObject(reply, "type", "set_stab_config_ack");
      cJSON_AddBoolToObject(reply, "ok", ok);
      if (ok) {
        cJSON_AddBoolToObject(reply, "enabled", applied.enabled);
        cJSON_AddNumberToObject(reply, "madgwick_beta", applied.madgwick_beta);
        cJSON_AddNumberToObject(reply, "lpf_cutoff_hz", applied.lpf_cutoff_hz);
        cJSON_AddNumberToObject(reply, "mode", applied.mode);
        cJSON_AddNumberToObject(reply, "pid_kp", applied.pid_kp);
        cJSON_AddNumberToObject(reply, "pid_ki", applied.pid_ki);
        cJSON_AddNumberToObject(reply, "pid_kd", applied.pid_kd);
        cJSON_AddNumberToObject(reply, "pid_max_correction",
                                applied.pid_max_correction);
        cJSON_AddNumberToObject(reply, "steer_to_yaw_rate_dps",
                                applied.steer_to_yaw_rate_dps);
        cJSON_AddNumberToObject(reply, "fade_ms", applied.fade_ms);
        cJSON_AddBoolToObject(reply, "pitch_comp_enabled",
                              applied.pitch_comp_enabled);
        cJSON_AddNumberToObject(reply, "pitch_comp_gain",
                                applied.pitch_comp_gain);
        cJSON_AddNumberToObject(reply, "pitch_comp_max_correction",
                                applied.pitch_comp_max_correction);
      }
      ws_send_reply(req, reply);
      cJSON_Delete(reply);
    }

    ESP_LOGI(TAG,
             "WS: set_stab_config -> %s "
             "(enabled=%d beta=%.3f cutoff=%.1f mode=%d kp=%.3f ki=%.3f "
             "kd=%.4f)",
             ok ? "OK" : "FAILED", applied.enabled, applied.madgwick_beta,
             applied.lpf_cutoff_hz, applied.mode, applied.pid_kp,
             applied.pid_ki, applied.pid_kd);
  }
}

extern "C" void app_main(void) {
  ESP_LOGI(TAG, "RC Vehicle ESP32-S3 firmware starting...");

  // Инициализация Wi-Fi AP
  ESP_LOGI(TAG, "Initializing Wi-Fi AP...");
  if (WiFiApInit() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize Wi-Fi AP");
    return;
  }

  // Инициализация HTTP сервера
  ESP_LOGI(TAG, "Initializing HTTP server...");
  if (HttpServerInit() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize HTTP server");
    return;
  }

  // Инициализация управления (PWM/RC/IMU/failsafe + телеметрия)
  ESP_LOGI(TAG, "Initializing vehicle control...");
  if (VehicleControlInit() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize vehicle control");
    return;
  }

  // WebSocket команды управления → local control loop
  WebSocketSetCommandHandler(&ws_cmd_handler);
  // WebSocket JSON-команды (калибровка и т.д.)
  WebSocketSetJsonHandler(&ws_json_handler);

  // Регистрация WebSocket URI на HTTP-сервере (один httpd на порту 80)
  ESP_LOGI(TAG, "Registering WebSocket handler...");
  if (WebSocketRegisterUri(HttpServerGetHandle()) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register WebSocket handler");
    return;
  }

  ESP_LOGI(TAG, "All systems initialized. Ready for connections.");

  char ap_ip[16];
  if (WiFiApGetIp(ap_ip, sizeof(ap_ip)) == ESP_OK) {
    ESP_LOGI(TAG, "----------------------------------------");
    ESP_LOGI(TAG, "  Подключитесь к Wi-Fi и откройте в браузере:");
    ESP_LOGI(TAG, "  http://%s", ap_ip);
    ESP_LOGI(TAG, "  WebSocket: ws://%s/ws", ap_ip);
    ESP_LOGI(TAG, "----------------------------------------");
  }

  // Основной поток — idle
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
