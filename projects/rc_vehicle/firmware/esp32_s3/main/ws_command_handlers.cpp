#include "ws_command_handlers.hpp"

#include <cstring>

#include "esp_log.h"
#include "i_vehicle_control.hpp"
#include "self_test.hpp"
#include "stabilization_config.hpp"
#include "stabilization_config_json.hpp"
#include "telemetry_log.hpp"
#include "com_offset_calibration.hpp"
#include "test_runner.hpp"
#include "udp_telem_sender.hpp"
#include "ws_command_registry.hpp"

static const char* TAG = "ws_handlers";

namespace rc_vehicle {

void HandleCalibrateImu(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  cJSON* mode_item = cJSON_GetObjectItem(json, "mode");
  const char* mode_str = (mode_item && cJSON_IsString(mode_item))
                             ? mode_item->valuestring
                             : "gyro";
  bool is_forward = (strcmp(mode_str, "forward") == 0);
  bool is_auto_forward = (strcmp(mode_str, "auto_forward") == 0);
  bool full = (strcmp(mode_str, "full") == 0);

  cJSON* reply = cJSON_CreateObject();
  if (reply) {
    cJSON_AddStringToObject(reply, "type", "calibrate_imu_ack");
    if (is_auto_forward) {
      // Поддержка target_accel (новый) и throttle (обратная совместимость)
      cJSON* accel_item = cJSON_GetObjectItem(json, "target_accel");
      cJSON* thr_item = cJSON_GetObjectItem(json, "throttle");
      float target_accel = 0.1f;  // default 0.1g
      if (accel_item && cJSON_IsNumber(accel_item)) {
        target_accel = (float)accel_item->valuedouble;
      } else if (thr_item && cJSON_IsNumber(thr_item)) {
        // Обратная совместимость: throttle 0.25 ≈ 0.1g
        target_accel = (float)thr_item->valuedouble * 0.4f;
      }
      bool ok = vc.StartAutoForwardCalibration(target_accel);
      cJSON_AddStringToObject(reply, "status", ok ? "collecting" : "failed");
      cJSON_AddNumberToObject(reply, "stage", 2);
      cJSON_AddBoolToObject(reply, "ok", ok);
      cJSON_AddBoolToObject(reply, "auto_drive", ok);
      cJSON_AddNumberToObject(reply, "target_accel", target_accel);
      ESP_LOGI(TAG,
               "calibrate_imu mode=auto_forward target_accel=%.3fg -> %s",
               target_accel, ok ? "started" : "failed (need stage 1 full)");
    } else if (is_forward) {
      bool ok = vc.StartForwardCalibration();
      cJSON_AddStringToObject(reply, "status", ok ? "collecting" : "failed");
      cJSON_AddNumberToObject(reply, "stage", 2);
      cJSON_AddBoolToObject(reply, "ok", ok);
      ESP_LOGI(TAG, "calibrate_imu mode=forward -> %s",
               ok ? "stage 2 started" : "failed (need stage 1 full)");
    } else {
      vc.StartCalibration(full);
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

void HandleGetCalibStatus(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  (void)json;

  cJSON* reply = cJSON_CreateObject();
  if (reply) {
    cJSON_AddStringToObject(reply, "type", "calib_status");
    cJSON_AddStringToObject(reply, "status", vc.GetCalibStatus());
    cJSON_AddNumberToObject(reply, "stage", vc.GetCalibStage());
    WsSendJsonReply(req, reply);
    cJSON_Delete(reply);
  }
}

void HandleSetForwardDirection(IVehicleControl& vc, cJSON* json,
                               httpd_req_t* req) {
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
  vc.SetForwardDirection(fx, fy, fz);

  cJSON* reply = cJSON_CreateObject();
  if (reply) {
    cJSON_AddStringToObject(reply, "type", "set_forward_direction_ack");
    cJSON_AddBoolToObject(reply, "ok", true);
    WsSendJsonReply(req, reply);
    cJSON_Delete(reply);
  }
}

void HandleGetStabConfig(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  (void)json;

  const auto& cfg = vc.GetStabilizationConfig();
  cJSON* reply = StabilizationConfigToJson(cfg);
  if (reply) {
    cJSON_AddStringToObject(reply, "type", "stab_config");
    WsSendJsonReply(req, reply);
    cJSON_Delete(reply);
  }
}

void HandleSetStabConfig(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  // Получаем текущую конфигурацию
  StabilizationConfig cfg = vc.GetStabilizationConfig();

  // Обновляем только переданные поля
  cJSON* mode_item = cJSON_GetObjectItem(json, "mode");
  if (mode_item && cJSON_IsNumber(mode_item)) {
    cfg.mode = static_cast<DriveMode>(static_cast<int>(mode_item->valuedouble));
  }

  // Остальные поля обновляем только если они есть в JSON
  StabilizationConfigFromJson(cfg, json);

  bool ok = vc.SetStabilizationConfig(cfg, true);

  // Get applied configuration (mode defaults may be applied)
  const auto& applied = vc.GetStabilizationConfig();
  cJSON* reply = ok ? StabilizationConfigToJson(applied) : cJSON_CreateObject();
  if (reply) {
    cJSON_AddStringToObject(reply, "type", "set_stab_config_ack");
    cJSON_AddBoolToObject(reply, "ok", ok);
    WsSendJsonReply(req, reply);
    cJSON_Delete(reply);
  }

  ESP_LOGI(TAG,
           "set_stab_config -> %s (mode=%s enabled=%d beta=%.3f cutoff=%.1f "
           "kp=%.3f ki=%.3f kd=%.4f)",
           ok ? "OK" : "FAILED", DriveModeToString(applied.mode),
           applied.enabled, applied.filter.madgwick_beta,
           applied.filter.lpf_cutoff_hz, applied.yaw_rate.pid.kp,
           applied.yaw_rate.pid.ki, applied.yaw_rate.pid.kd);
}

void HandleGetLogInfo(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  (void)json;

  size_t count = 0, cap = 0;
  vc.GetLogInfo(count, cap);

  cJSON* reply = cJSON_CreateObject();
  if (reply) {
    cJSON_AddStringToObject(reply, "type", "log_info");
    cJSON_AddNumberToObject(reply, "count", (double)count);
    cJSON_AddNumberToObject(reply, "capacity", (double)cap);
    WsSendJsonReply(req, reply);
    cJSON_Delete(reply);
  }
}

void HandleGetLogData(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  size_t total_count = 0, cap = 0;
  vc.GetLogInfo(total_count, cap);

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
      TelemetryLogFrame frame;
      for (size_t i = 0; i < req_count; ++i) {
        if (vc.GetLogFrame(offset + i, frame)) {
          cJSON* f = cJSON_CreateObject();
          if (f) {
            cJSON_AddNumberToObject(f, "ts_ms", frame.ts_ms);
            cJSON_AddNumberToObject(f, "ax", frame.ax);
            cJSON_AddNumberToObject(f, "ay", frame.ay);
            cJSON_AddNumberToObject(f, "az", frame.az);
            cJSON_AddNumberToObject(f, "gx", frame.gx);
            cJSON_AddNumberToObject(f, "gy", frame.gy);
            cJSON_AddNumberToObject(f, "gz", frame.gz);
            cJSON_AddNumberToObject(f, "vx", frame.vx);
            cJSON_AddNumberToObject(f, "vy", frame.vy);
            cJSON_AddNumberToObject(f, "slip_deg", frame.slip_deg);
            cJSON_AddNumberToObject(f, "speed_ms", frame.speed_ms);
            cJSON_AddNumberToObject(f, "throttle", frame.throttle);
            cJSON_AddNumberToObject(f, "steering", frame.steering);
            cJSON_AddNumberToObject(f, "pitch_deg", frame.pitch_deg);
            cJSON_AddNumberToObject(f, "roll_deg", frame.roll_deg);
            cJSON_AddNumberToObject(f, "yaw_deg", frame.yaw_deg);
            cJSON_AddNumberToObject(f, "yaw_rate_dps", frame.yaw_rate_dps);
            cJSON_AddBoolToObject(f, "oversteer_active", frame.oversteer_active);
            cJSON_AddNumberToObject(f, "rc_throttle", frame.rc_throttle);
            cJSON_AddNumberToObject(f, "rc_steering", frame.rc_steering);
            cJSON_AddNumberToObject(f, "cmd_throttle", frame.cmd_throttle);
            cJSON_AddNumberToObject(f, "cmd_steering", frame.cmd_steering);
            cJSON_AddNumberToObject(f, "ekf_vx_var", frame.ekf_vx_var);
            cJSON_AddNumberToObject(f, "ekf_vy_var", frame.ekf_vy_var);
            cJSON_AddNumberToObject(f, "ekf_r_var", frame.ekf_r_var);
            cJSON_AddNumberToObject(f, "test_marker", frame.test_marker);
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

void HandleClearLog(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  (void)json;

  vc.ClearLog();

  cJSON* reply = cJSON_CreateObject();
  if (reply) {
    cJSON_AddStringToObject(reply, "type", "clear_log_ack");
    cJSON_AddBoolToObject(reply, "ok", true);
    WsSendJsonReply(req, reply);
    cJSON_Delete(reply);
  }
}

void HandleSetKidsPreset(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  cJSON* preset_item = cJSON_GetObjectItem(json, "preset");
  if (!preset_item || !cJSON_IsNumber(preset_item)) {
    cJSON* reply = cJSON_CreateObject();
    if (reply) {
      cJSON_AddStringToObject(reply, "type", "set_kids_preset_ack");
      cJSON_AddBoolToObject(reply, "ok", false);
      cJSON_AddStringToObject(reply, "error", "missing or invalid preset");
      WsSendJsonReply(req, reply);
      cJSON_Delete(reply);
    }
    return;
  }

  int preset_val = preset_item->valueint;
  if (preset_val < 0 || preset_val > 3) {
    cJSON* reply = cJSON_CreateObject();
    if (reply) {
      cJSON_AddStringToObject(reply, "type", "set_kids_preset_ack");
      cJSON_AddBoolToObject(reply, "ok", false);
      cJSON_AddStringToObject(reply, "error", "preset out of range (0-3)");
      WsSendJsonReply(req, reply);
      cJSON_Delete(reply);
    }
    return;
  }

  StabilizationConfig cfg = vc.GetStabilizationConfig();
  cfg.kids_mode.ApplyPreset(static_cast<KidsPreset>(preset_val));
  bool ok = vc.SetStabilizationConfig(cfg, true);

  const auto& applied = vc.GetStabilizationConfig();
  cJSON* reply = ok ? StabilizationConfigToJson(applied) : cJSON_CreateObject();
  if (reply) {
    cJSON_AddStringToObject(reply, "type", "set_kids_preset_ack");
    cJSON_AddBoolToObject(reply, "ok", ok);
    WsSendJsonReply(req, reply);
    cJSON_Delete(reply);
  }

  ESP_LOGI(TAG, "set_kids_preset preset=%d -> %s", preset_val,
           ok ? "OK" : "FAILED");
}

void HandleToggleKidsMode(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  cJSON* active_item = cJSON_GetObjectItem(json, "active");
  bool active = (active_item && cJSON_IsBool(active_item))
                    ? cJSON_IsTrue(active_item)
                    : false;

  vc.SetKidsModeActive(active);

  cJSON* reply = cJSON_CreateObject();
  if (reply) {
    cJSON_AddStringToObject(reply, "type", "toggle_kids_mode_ack");
    cJSON_AddBoolToObject(reply, "active", active);
    WsSendJsonReply(req, reply);
    cJSON_Delete(reply);
  }

  ESP_LOGI(TAG, "toggle_kids_mode active=%s -> OK", active ? "true" : "false");
}

void HandleGetKidsPresets(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  (void)vc;
  (void)json;

  cJSON* reply = cJSON_CreateObject();
  if (reply) {
    cJSON_AddStringToObject(reply, "type", "kids_presets");

    cJSON* presets = cJSON_CreateArray();
    if (presets) {
      // Preset 0: Custom
      cJSON* custom = cJSON_CreateObject();
      if (custom) {
        cJSON_AddNumberToObject(custom, "id", 0);
        cJSON_AddStringToObject(custom, "name", "Custom");
        cJSON_AddStringToObject(custom, "description", "User-defined settings");
        cJSON_AddItemToArray(presets, custom);
      }

      // Preset 1: Toddler (3-5 years)
      cJSON* toddler = cJSON_CreateObject();
      if (toddler) {
        cJSON_AddNumberToObject(toddler, "id", 1);
        cJSON_AddStringToObject(toddler, "name", "Toddler");
        cJSON_AddStringToObject(toddler, "description", "3-5 years old");
        cJSON_AddNumberToObject(toddler, "throttle_limit", 0.2f);
        cJSON_AddNumberToObject(toddler, "steering_limit", 0.5f);
        cJSON_AddItemToArray(presets, toddler);
      }

      // Preset 2: Child (6-9 years)
      cJSON* child = cJSON_CreateObject();
      if (child) {
        cJSON_AddNumberToObject(child, "id", 2);
        cJSON_AddStringToObject(child, "name", "Child");
        cJSON_AddStringToObject(child, "description", "6-9 years old");
        cJSON_AddNumberToObject(child, "throttle_limit", 0.3f);
        cJSON_AddNumberToObject(child, "steering_limit", 0.7f);
        cJSON_AddItemToArray(presets, child);
      }

      // Preset 3: Preteen (10-12 years)
      cJSON* preteen = cJSON_CreateObject();
      if (preteen) {
        cJSON_AddNumberToObject(preteen, "id", 3);
        cJSON_AddStringToObject(preteen, "name", "Preteen");
        cJSON_AddStringToObject(preteen, "description", "10-12 years old");
        cJSON_AddNumberToObject(preteen, "throttle_limit", 0.5f);
        cJSON_AddNumberToObject(preteen, "steering_limit", 0.85f);
        cJSON_AddItemToArray(presets, preteen);
      }

      cJSON_AddItemToObject(reply, "presets", presets);
    }

    WsSendJsonReply(req, reply);
    cJSON_Delete(reply);
  }
}

void HandleCalibrateSteeringTrim(IVehicleControl& vc, cJSON* json,
                                 httpd_req_t* req) {
  cJSON* accel_item = cJSON_GetObjectItem(json, "target_accel");
  float target_accel = 0.1f;
  if (accel_item && cJSON_IsNumber(accel_item)) {
    target_accel = (float)accel_item->valuedouble;
  }

  bool ok = vc.StartSteeringTrimCalibration(target_accel);

  cJSON* reply = cJSON_CreateObject();
  if (reply) {
    cJSON_AddStringToObject(reply, "type", "calibrate_steering_trim_ack");
    cJSON_AddBoolToObject(reply, "ok", ok);
    cJSON_AddStringToObject(reply, "status", ok ? "started" : "failed");
    if (!ok) {
      cJSON_AddStringToObject(
          reply, "error",
          "IMU not ready, another calibration active, or already running");
    }
    cJSON_AddNumberToObject(reply, "target_accel", target_accel);
    WsSendJsonReply(req, reply);
    cJSON_Delete(reply);
  }

  ESP_LOGI(TAG, "calibrate_steering_trim target_accel=%.3fg -> %s",
           target_accel, ok ? "started" : "failed");
}

void HandleGetSteeringTrimStatus(IVehicleControl& vc, cJSON* json,
                                 httpd_req_t* req) {
  (void)json;

  bool active = vc.IsSteeringTrimCalibActive();
  auto result = vc.GetSteeringTrimCalibResult();

  cJSON* reply = cJSON_CreateObject();
  if (reply) {
    cJSON_AddStringToObject(reply, "type", "steering_trim_status");
    cJSON_AddBoolToObject(reply, "active", active);

    cJSON* res = cJSON_CreateObject();
    if (res) {
      cJSON_AddBoolToObject(res, "valid", result.valid);
      cJSON_AddNumberToObject(res, "trim", result.trim);
      cJSON_AddNumberToObject(res, "mean_yaw_rate", result.mean_yaw_rate);
      cJSON_AddNumberToObject(res, "samples", result.samples);
      cJSON_AddItemToObject(reply, "result", res);
    }

    WsSendJsonReply(req, reply);
    cJSON_Delete(reply);
  }
}

void HandleCalibrateComOffset(IVehicleControl& vc, cJSON* json,
                              httpd_req_t* req) {
  cJSON* accel_item = cJSON_GetObjectItem(json, "target_accel");
  cJSON* steer_item = cJSON_GetObjectItem(json, "steering");
  cJSON* dur_item = cJSON_GetObjectItem(json, "duration");
  float target_accel = 0.1f;
  float steering = 0.5f;
  float duration = 5.0f;
  if (accel_item && cJSON_IsNumber(accel_item))
    target_accel = (float)accel_item->valuedouble;
  if (steer_item && cJSON_IsNumber(steer_item))
    steering = (float)steer_item->valuedouble;
  if (dur_item && cJSON_IsNumber(dur_item))
    duration = (float)dur_item->valuedouble;

  bool ok = vc.StartComOffsetCalibration(target_accel, steering, duration);

  cJSON* reply = cJSON_CreateObject();
  if (reply) {
    cJSON_AddStringToObject(reply, "type", "calibrate_com_offset_ack");
    cJSON_AddBoolToObject(reply, "ok", ok);
    cJSON_AddStringToObject(reply, "status", ok ? "started" : "failed");
    if (!ok) {
      cJSON_AddStringToObject(
          reply, "error",
          "IMU not ready, another procedure active, or already running");
    }
    WsSendJsonReply(req, reply);
    cJSON_Delete(reply);
  }

  ESP_LOGI(TAG,
           "calibrate_com_offset accel=%.3fg steer=%.2f dur=%.1fs -> %s",
           target_accel, steering, duration, ok ? "started" : "failed");
}

void HandleGetComOffsetStatus(IVehicleControl& vc, cJSON* json,
                              httpd_req_t* req) {
  (void)json;

  bool active = vc.IsComOffsetCalibActive();
  auto result = vc.GetComOffsetCalibResult();

  cJSON* reply = cJSON_CreateObject();
  if (reply) {
    cJSON_AddStringToObject(reply, "type", "com_offset_status");
    cJSON_AddBoolToObject(reply, "active", active);

    cJSON* res = cJSON_CreateObject();
    if (res) {
      cJSON_AddBoolToObject(res, "valid", result.valid);
      cJSON_AddNumberToObject(res, "rx", result.rx);
      cJSON_AddNumberToObject(res, "ry", result.ry);
      cJSON_AddNumberToObject(res, "omega_cw_dps", result.omega_cw_dps);
      cJSON_AddNumberToObject(res, "omega_ccw_dps", result.omega_ccw_dps);
      cJSON_AddNumberToObject(res, "samples_cw", result.samples_cw);
      cJSON_AddNumberToObject(res, "samples_ccw", result.samples_ccw);
      cJSON_AddItemToObject(reply, "result", res);
    }

    WsSendJsonReply(req, reply);
    cJSON_Delete(reply);
  }
}

void HandleStartTest(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  TestParams params;

  cJSON* type_item = cJSON_GetObjectItem(json, "test_type");
  if (type_item && cJSON_IsString(type_item)) {
    const char* t = type_item->valuestring;
    if (strcmp(t, "circle") == 0)
      params.type = TestType::Circle;
    else if (strcmp(t, "step") == 0)
      params.type = TestType::Step;
    else
      params.type = TestType::Straight;
  }

  cJSON* accel_item = cJSON_GetObjectItem(json, "target_accel");
  if (accel_item && cJSON_IsNumber(accel_item)) {
    params.target_accel_g = (float)accel_item->valuedouble;
  }

  cJSON* dur_item = cJSON_GetObjectItem(json, "duration");
  if (dur_item && cJSON_IsNumber(dur_item)) {
    params.duration_sec = (float)dur_item->valuedouble;
  }

  cJSON* steer_item = cJSON_GetObjectItem(json, "steering");
  if (steer_item && cJSON_IsNumber(steer_item)) {
    params.steering = (float)steer_item->valuedouble;
  }

  bool ok = vc.StartTest(params);

  cJSON* reply = cJSON_CreateObject();
  if (reply) {
    cJSON_AddStringToObject(reply, "type", "start_test_ack");
    cJSON_AddBoolToObject(reply, "ok", ok);
    const char* type_str = "straight";
    if (params.type == TestType::Circle) type_str = "circle";
    else if (params.type == TestType::Step) type_str = "step";
    cJSON_AddStringToObject(reply, "test_type", type_str);
    if (!ok) {
      cJSON_AddStringToObject(reply, "error",
          "IMU not ready, another procedure active, or test already running");
    }
    WsSendJsonReply(req, reply);
    cJSON_Delete(reply);
  }

  ESP_LOGI(TAG, "start_test type=%s accel=%.3fg dur=%.1fs steer=%.2f -> %s",
           params.type == TestType::Circle ? "circle" :
           params.type == TestType::Step ? "step" : "straight",
           params.target_accel_g, params.duration_sec, params.steering,
           ok ? "started" : "failed");
}

void HandleStopTest(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  (void)json;
  vc.StopTest();

  cJSON* reply = cJSON_CreateObject();
  if (reply) {
    cJSON_AddStringToObject(reply, "type", "stop_test_ack");
    cJSON_AddBoolToObject(reply, "ok", true);
    WsSendJsonReply(req, reply);
    cJSON_Delete(reply);
  }

  ESP_LOGI(TAG, "stop_test");
}

void HandleGetTestStatus(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  (void)json;

  bool active = vc.IsTestActive();
  auto status = vc.GetTestStatus();

  cJSON* reply = cJSON_CreateObject();
  if (reply) {
    cJSON_AddStringToObject(reply, "type", "test_status");
    cJSON_AddBoolToObject(reply, "active", active);

    const char* phase_str = "idle";
    switch (status.phase) {
      case TestRunner::Phase::Accelerate: phase_str = "accelerate"; break;
      case TestRunner::Phase::Cruise: phase_str = "cruise"; break;
      case TestRunner::Phase::StepExec: phase_str = "step_exec"; break;
      case TestRunner::Phase::Brake: phase_str = "brake"; break;
      case TestRunner::Phase::Done: phase_str = "done"; break;
      case TestRunner::Phase::Failed: phase_str = "failed"; break;
      default: break;
    }
    cJSON_AddStringToObject(reply, "phase", phase_str);

    const char* type_str = "straight";
    if (status.type == TestType::Circle) type_str = "circle";
    else if (status.type == TestType::Step) type_str = "step";
    cJSON_AddStringToObject(reply, "test_type", type_str);

    cJSON_AddNumberToObject(reply, "elapsed", status.elapsed_sec);
    cJSON_AddNumberToObject(reply, "phase_elapsed", status.phase_elapsed_sec);
    cJSON_AddBoolToObject(reply, "valid", status.valid);

    WsSendJsonReply(req, reply);
    cJSON_Delete(reply);
  }
}

void HandleStartSpeedCalib(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  cJSON* thr_item = cJSON_GetObjectItem(json, "throttle");
  cJSON* dur_item = cJSON_GetObjectItem(json, "duration");
  float throttle = 0.3f;
  float duration = 3.0f;
  if (thr_item && cJSON_IsNumber(thr_item))
    throttle = (float)thr_item->valuedouble;
  if (dur_item && cJSON_IsNumber(dur_item))
    duration = (float)dur_item->valuedouble;

  bool ok = vc.StartSpeedCalibration(throttle, duration);

  cJSON* reply = cJSON_CreateObject();
  if (reply) {
    cJSON_AddStringToObject(reply, "type", "start_speed_calib_ack");
    cJSON_AddBoolToObject(reply, "ok", ok);
    cJSON_AddStringToObject(reply, "status", ok ? "started" : "failed");
    if (!ok) {
      cJSON_AddStringToObject(
          reply, "error",
          "IMU not ready, another procedure active, or already running");
    }
    cJSON_AddNumberToObject(reply, "throttle", throttle);
    cJSON_AddNumberToObject(reply, "duration", duration);
    WsSendJsonReply(req, reply);
    cJSON_Delete(reply);
  }

  ESP_LOGI(TAG, "start_speed_calib throttle=%.2f dur=%.1fs -> %s", throttle,
           duration, ok ? "started" : "failed");
}

void HandleStopSpeedCalib(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  (void)json;
  vc.StopSpeedCalibration();

  cJSON* reply = cJSON_CreateObject();
  if (reply) {
    cJSON_AddStringToObject(reply, "type", "stop_speed_calib_ack");
    cJSON_AddBoolToObject(reply, "ok", true);
    WsSendJsonReply(req, reply);
    cJSON_Delete(reply);
  }

  ESP_LOGI(TAG, "stop_speed_calib");
}

void HandleGetSpeedCalibStatus(IVehicleControl& vc, cJSON* json,
                                httpd_req_t* req) {
  (void)json;

  bool active = vc.IsSpeedCalibActive();
  auto result = vc.GetSpeedCalibResult();

  cJSON* reply = cJSON_CreateObject();
  if (reply) {
    cJSON_AddStringToObject(reply, "type", "speed_calib_status");
    cJSON_AddBoolToObject(reply, "active", active);

    cJSON* res = cJSON_CreateObject();
    if (res) {
      cJSON_AddBoolToObject(res, "valid", result.valid);
      cJSON_AddNumberToObject(res, "target_throttle", result.target_throttle);
      cJSON_AddNumberToObject(res, "mean_speed_ms", result.mean_speed_ms);
      cJSON_AddNumberToObject(res, "speed_gain", result.speed_gain);
      cJSON_AddNumberToObject(res, "samples", result.samples);
      cJSON_AddItemToObject(reply, "result", res);
    }

    WsSendJsonReply(req, reply);
    cJSON_Delete(reply);
  }
}

void HandleRunSelfTest(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  (void)json;

  auto results = vc.RunSelfTest();
  bool all_passed = rc_vehicle::SelfTest::AllPassed(results);

  cJSON* reply = cJSON_CreateObject();
  if (reply) {
    cJSON_AddStringToObject(reply, "type", "self_test_result");
    cJSON_AddBoolToObject(reply, "passed", all_passed);

    cJSON* tests_arr = cJSON_CreateArray();
    if (tests_arr) {
      for (const auto& item : results) {
        cJSON* t = cJSON_CreateObject();
        if (t) {
          cJSON_AddStringToObject(t, "name", item.name);
          cJSON_AddBoolToObject(t, "passed", item.passed);
          cJSON_AddStringToObject(t, "value", item.value);
          cJSON_AddItemToArray(tests_arr, t);
        }
      }
      cJSON_AddItemToObject(reply, "tests", tests_arr);
    }

    WsSendJsonReply(req, reply);
    cJSON_Delete(reply);
  }

  ESP_LOGI(TAG, "run_self_test -> %s (%zu checks)",
           all_passed ? "ALL PASS" : "FAIL", results.size());
}

void HandleUdpStreamStart(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  (void)vc;
  cJSON* ip_item = cJSON_GetObjectItem(json, "ip");
  cJSON* port_item = cJSON_GetObjectItem(json, "port");
  cJSON* hz_item = cJSON_GetObjectItem(json, "hz");

  const char* ip = (ip_item && cJSON_IsString(ip_item)) ? ip_item->valuestring
                                                        : nullptr;
  uint16_t port = (port_item && cJSON_IsNumber(port_item))
                      ? (uint16_t)port_item->valueint
                      : 5555;
  uint8_t hz = (hz_item && cJSON_IsNumber(hz_item)) ? (uint8_t)hz_item->valueint
                                                    : 100;

  cJSON* reply = cJSON_CreateObject();
  if (reply) {
    cJSON_AddStringToObject(reply, "type", "udp_stream_start_ack");
    if (!ip) {
      cJSON_AddBoolToObject(reply, "ok", false);
      cJSON_AddStringToObject(reply, "error", "missing ip field");
    } else {
      bool ok = UdpTelemStart(ip, port, hz);
      cJSON_AddBoolToObject(reply, "ok", ok);
      if (ok) {
        cJSON_AddStringToObject(reply, "ip", ip);
        cJSON_AddNumberToObject(reply, "port", port);
        cJSON_AddNumberToObject(reply, "hz", hz);
      } else {
        cJSON_AddStringToObject(reply, "error", "invalid parameters");
      }
    }
    WsSendJsonReply(req, reply);
    cJSON_Delete(reply);
  }

  ESP_LOGI(TAG, "udp_stream_start ip=%s port=%u hz=%u", ip ? ip : "null", port,
           hz);
}

void HandleUdpStreamStop(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  (void)vc;
  (void)json;
  UdpTelemStop();

  cJSON* reply = cJSON_CreateObject();
  if (reply) {
    cJSON_AddStringToObject(reply, "type", "udp_stream_stop_ack");
    cJSON_AddBoolToObject(reply, "ok", true);
    WsSendJsonReply(req, reply);
    cJSON_Delete(reply);
  }

  ESP_LOGI(TAG, "udp_stream_stop");
}

void HandleUdpStreamStatus(IVehicleControl& vc, cJSON* json,
                           httpd_req_t* req) {
  (void)vc;
  (void)json;

  cJSON* reply = cJSON_CreateObject();
  if (reply) {
    cJSON_AddStringToObject(reply, "type", "udp_stream_status");
    cJSON_AddBoolToObject(reply, "streaming", UdpTelemIsStreaming());
    cJSON_AddStringToObject(reply, "ip", UdpTelemGetTargetIp());
    cJSON_AddNumberToObject(reply, "port", UdpTelemGetTargetPort());
    cJSON_AddNumberToObject(reply, "hz", UdpTelemGetHz());
    cJSON_AddNumberToObject(reply, "seq", (double)UdpTelemGetSeq());
    cJSON_AddNumberToObject(reply, "dropped", (double)UdpTelemGetDropped());
    WsSendJsonReply(req, reply);
    cJSON_Delete(reply);
  }
}

void HandleCalibrateMag(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  cJSON* action_item = cJSON_GetObjectItem(json, "action");
  const char* action = (action_item && cJSON_IsString(action_item))
                           ? action_item->valuestring
                           : "";

  bool ok = true;
  if (strcmp(action, "start") == 0) {
    vc.StartMagCalibration();
    ESP_LOGI(TAG, "calibrate_mag: start");
  } else if (strcmp(action, "finish") == 0) {
    vc.FinishMagCalibration();
    ESP_LOGI(TAG, "calibrate_mag: finish -> %s", vc.GetMagCalibStatus());
  } else if (strcmp(action, "cancel") == 0) {
    vc.CancelMagCalibration();
    ESP_LOGI(TAG, "calibrate_mag: cancel");
  } else if (strcmp(action, "erase") == 0) {
    ok = vc.EraseMagCalibration();
    ESP_LOGI(TAG, "calibrate_mag: erase -> %s", ok ? "ok" : "failed");
  } else {
    ok = false;
    ESP_LOGW(TAG, "calibrate_mag: unknown action '%s'", action);
  }

  cJSON* reply = cJSON_CreateObject();
  if (reply) {
    cJSON_AddStringToObject(reply, "type", "calibrate_mag_ack");
    cJSON_AddStringToObject(reply, "status", vc.GetMagCalibStatus());
    cJSON_AddStringToObject(reply, "fail_reason", vc.GetMagCalibFailReason());
    cJSON_AddBoolToObject(reply, "ok", ok);
    WsSendJsonReply(req, reply);
    cJSON_Delete(reply);
  }
}

void HandleGetMagCalibStatus(IVehicleControl& vc, cJSON* json,
                              httpd_req_t* req) {
  (void)json;

  cJSON* reply = cJSON_CreateObject();
  if (reply) {
    cJSON_AddStringToObject(reply, "type", "mag_calib_status");
    cJSON_AddStringToObject(reply, "status", vc.GetMagCalibStatus());
    cJSON_AddStringToObject(reply, "fail_reason", vc.GetMagCalibFailReason());
    WsSendJsonReply(req, reply);
    cJSON_Delete(reply);
  }
}

void HandleResetHeadingRef(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  (void)json;
  vc.ResetHeadingRef();

  cJSON* reply = cJSON_CreateObject();
  if (reply) {
    cJSON_AddStringToObject(reply, "type", "reset_heading_ref_ack");
    cJSON_AddBoolToObject(reply, "ok", true);
    cJSON_AddStringToObject(reply, "status", "heading ref reset");
    WsSendJsonReply(req, reply);
    cJSON_Delete(reply);
  }

  ESP_LOGI(TAG, "reset_heading_ref");
}

}  // namespace rc_vehicle
