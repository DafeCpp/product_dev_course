#include "ws_command_handlers.hpp"

#include <cstring>

#include "esp_log.h"
#include "stabilization_config_json.hpp"
#include "telemetry_log.hpp"
#include "vehicle_control.hpp"
#include "ws_command_registry.hpp"

static const char* TAG = "ws_handlers";

namespace rc_vehicle {

void HandleCalibrateImu(cJSON* json, httpd_req_t* req) {
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
      ESP_LOGI(TAG, "calibrate_imu mode=forward -> %s",
               ok ? "stage 2 started" : "failed (need stage 1 full)");
    } else {
      VehicleControlStartCalibration(full);
      cJSON_AddStringToObject(reply, "status", "collecting");
      cJSON_AddNumberToObject(reply, "stage", 1);
      cJSON_AddBoolToObject(reply, "ok", true);
      cJSON_AddStringToObject(reply, "mode", full ? "full" : "gyro");
      ESP_LOGI(TAG, "calibrate_imu (stage 1, mode=%s)", full ? "full" : "gyro");
    }
    WsSendJsonReply(req, reply);
    cJSON_Delete(reply);
  }
}

void HandleGetCalibStatus(cJSON* json, httpd_req_t* req) {
  (void)json;  // Unused parameter

  cJSON* reply = cJSON_CreateObject();
  if (reply) {
    cJSON_AddStringToObject(reply, "type", "calib_status");
    cJSON_AddStringToObject(reply, "status", VehicleControlGetCalibStatus());
    cJSON_AddNumberToObject(reply, "stage", VehicleControlGetCalibStage());
    WsSendJsonReply(req, reply);
    cJSON_Delete(reply);
  }
}

void HandleSetForwardDirection(cJSON* json, httpd_req_t* req) {
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
    WsSendJsonReply(req, reply);
    cJSON_Delete(reply);
  }
}

void HandleGetStabConfig(cJSON* json, httpd_req_t* req) {
  (void)json;  // Unused parameter

  const auto& cfg = VehicleControlGetStabilizationConfig();
  cJSON* reply = StabilizationConfigToJson(cfg);
  if (reply) {
    cJSON_AddStringToObject(reply, "type", "stab_config");
    WsSendJsonReply(req, reply);
    cJSON_Delete(reply);
  }
}

void HandleSetStabConfig(cJSON* json, httpd_req_t* req) {
  StabilizationConfig cfg = VehicleControlGetStabilizationConfig();
  StabilizationConfigFromJson(cfg, json);
  bool ok = VehicleControlSetStabilizationConfig(cfg, true);

  // Get applied configuration (mode defaults may be applied)
  const auto& applied = VehicleControlGetStabilizationConfig();
  cJSON* reply = ok ? StabilizationConfigToJson(applied) : cJSON_CreateObject();
  if (reply) {
    cJSON_AddStringToObject(reply, "type", "set_stab_config_ack");
    cJSON_AddBoolToObject(reply, "ok", ok);
    WsSendJsonReply(req, reply);
    cJSON_Delete(reply);
  }

  ESP_LOGI(TAG,
           "set_stab_config -> %s "
           "(enabled=%d beta=%.3f cutoff=%.1f mode=%d kp=%.3f ki=%.3f kd=%.4f)",
           ok ? "OK" : "FAILED", applied.enabled, applied.filter.madgwick_beta,
           applied.filter.lpf_cutoff_hz, applied.mode, applied.yaw_rate.pid.kp,
           applied.yaw_rate.pid.ki, applied.yaw_rate.pid.kd);
}

void HandleGetLogInfo(cJSON* json, httpd_req_t* req) {
  (void)json;  // Unused parameter

  size_t count = 0, cap = 0;
  VehicleControlGetLogInfo(&count, &cap);

  cJSON* reply = cJSON_CreateObject();
  if (reply) {
    cJSON_AddStringToObject(reply, "type", "log_info");
    cJSON_AddNumberToObject(reply, "count", (double)count);
    cJSON_AddNumberToObject(reply, "capacity", (double)cap);
    WsSendJsonReply(req, reply);
    cJSON_Delete(reply);
  }
}

void HandleGetLogData(cJSON* json, httpd_req_t* req) {
  size_t total_count = 0, cap = 0;
  VehicleControlGetLogInfo(&total_count, &cap);

  cJSON* offset_j = cJSON_GetObjectItem(json, "offset");
  cJSON* count_j = cJSON_GetObjectItem(json, "count");
  size_t offset =
      (offset_j && cJSON_IsNumber(offset_j)) ? (size_t)offset_j->valueint : 0;
  size_t req_count =
      (count_j && cJSON_IsNumber(count_j)) ? (size_t)count_j->valueint : 100;

  // Limit to 200 frames per request
  if (req_count > 200) req_count = 200;
  if (offset >= total_count) req_count = 0;
  if (offset + req_count > total_count) req_count = total_count - offset;

  cJSON* reply = cJSON_CreateObject();
  if (reply) {
    cJSON_AddStringToObject(reply, "type", "log_data");
    cJSON* frames_arr = cJSON_CreateArray();
    if (frames_arr) {
      for (size_t i = 0; i < req_count; ++i) {
        TelemetryLogFrame frame;
        if (VehicleControlGetLogFrame(offset + i, &frame)) {
          cJSON* f = cJSON_CreateObject();
          if (f) {
            cJSON_AddNumberToObject(f, "ts_ms", frame.ts_ms);
            cJSON_AddNumberToObject(f, "vx", frame.vx);
            cJSON_AddNumberToObject(f, "vy", frame.vy);
            cJSON_AddNumberToObject(f, "slip_deg", frame.slip_deg);
            cJSON_AddNumberToObject(f, "speed_ms", frame.speed_ms);
            cJSON_AddNumberToObject(f, "throttle", frame.throttle);
            cJSON_AddNumberToObject(f, "steering", frame.steering);
            cJSON_AddItemToArray(frames_arr, f);
          }
        }
      }
      cJSON_AddItemToObject(reply, "frames", frames_arr);
    }
    WsSendJsonReply(req, reply);
    cJSON_Delete(reply);
  }
}

void HandleClearLog(cJSON* json, httpd_req_t* req) {
  (void)json;  // Unused parameter

  VehicleControlClearLog();

  cJSON* reply = cJSON_CreateObject();
  if (reply) {
    cJSON_AddStringToObject(reply, "type", "clear_log_ack");
    cJSON_AddBoolToObject(reply, "ok", true);
    WsSendJsonReply(req, reply);
    cJSON_Delete(reply);
  }
}

}  // namespace rc_vehicle