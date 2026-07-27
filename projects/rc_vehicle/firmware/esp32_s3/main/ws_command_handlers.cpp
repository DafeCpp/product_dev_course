#include "ws_command_handlers.hpp"

#include <cmath>
#include <cstring>

#include "com_offset_calibration.hpp"
#include "esp_log.h"
#include "i_vehicle_control.hpp"
#include "self_test.hpp"
#include "stabilization_config.hpp"
#include "stabilization_config_json.hpp"
#include "telemetry_log.hpp"
#include "test_runner.hpp"
#include "udp_telem_sender.hpp"
#include "ws_json_util.hpp"

static const char* TAG = "ws_handlers";

namespace rc_vehicle {

void HandleControlCmd(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  (void)req;  // fire-and-forget: ответа на "cmd" не было и раньше
  cJSON* throttle = cJSON_GetObjectItem(json, "throttle");
  cJSON* steer = cJSON_GetObjectItem(json, "steering");
  if (!throttle) throttle = cJSON_GetObjectItem(json, "thr");
  if (!steer) steer = cJSON_GetObjectItem(json, "steer");

  if (throttle && steer) {
    float thr = static_cast<float>(throttle->valuedouble);
    float str = static_cast<float>(steer->valuedouble);
    vc.OnWifiCommand(thr, str);
  }
}

void HandleCalibrateImu(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  const char* mode_str = JsonGetString(json, "mode", "gyro");
  bool is_forward = (strcmp(mode_str, "forward") == 0);
  bool is_auto_forward = (strcmp(mode_str, "auto_forward") == 0);
  bool full = (strcmp(mode_str, "full") == 0);

  WsReply(req, "calibrate_imu_ack", [&](cJSON* reply) {
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
      ESP_LOGI(TAG, "calibrate_imu mode=auto_forward target_accel=%.3fg -> %s",
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
  });
}

void HandleGetCalibStatus(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  (void)json;
  WsReply(req, "calib_status", [&](cJSON* reply) {
    cJSON_AddStringToObject(reply, "status", vc.GetCalibStatus());
    cJSON_AddNumberToObject(reply, "stage", vc.GetCalibStage());
  });
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

  WsReply(req, "set_forward_direction_ack",
          [](cJSON* reply) { cJSON_AddBoolToObject(reply, "ok", true); });
}

void HandleGetStabConfig(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  (void)json;
  const auto& cfg = vc.GetStabilizationConfig();
  WsReply(req, "stab_config", JsonDoc(StabilizationConfigToJson(cfg)));
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
  JsonDoc reply(ok ? StabilizationConfigToJson(applied) : cJSON_CreateObject());
  WsReply(req, "set_stab_config_ack", std::move(reply),
          [&](cJSON* r) { cJSON_AddBoolToObject(r, "ok", ok); });

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

  WsReply(req, "log_info", [&](cJSON* reply) {
    cJSON_AddNumberToObject(reply, "count", (double)count);
    cJSON_AddNumberToObject(reply, "capacity", (double)cap);
  });
}

void HandleGetLogData(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  size_t total_count = 0, cap = 0;
  vc.GetLogInfo(total_count, cap);

  size_t offset = (size_t)JsonGetInt(json, "offset", 0);
  size_t req_count = (size_t)JsonGetInt(json, "count", 100);

  // Limit to 200 frames per request
  if (req_count > 200) req_count = 200;
  if (offset >= total_count) req_count = 0;
  if (offset + req_count > total_count) req_count = total_count - offset;

  WsReply(req, "log_data", [&](cJSON* reply) {
    cJSON* frames_arr = cJSON_CreateArray();
    if (!frames_arr) return;
    TelemetryLogFrame frame;
    for (size_t i = 0; i < req_count; ++i) {
      if (!vc.GetLogFrame(offset + i, frame)) continue;
      cJSON* f = cJSON_CreateObject();
      if (!f) continue;
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
      cJSON_AddNumberToObject(f, "ekf_diverged", frame.ekf_diverged);
      cJSON_AddNumberToObject(f, "drive_mode", frame.drive_mode);
      cJSON_AddNumberToObject(f, "stab_enabled", frame.stab_enabled);
      cJSON_AddItemToArray(frames_arr, f);
    }
    cJSON_AddItemToObject(reply, "frames", frames_arr);
  });
}

void HandleClearLog(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  (void)json;
  vc.ClearLog();

  WsReply(req, "clear_log_ack",
          [](cJSON* reply) { cJSON_AddBoolToObject(reply, "ok", true); });
}

void HandleSetKidsPreset(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  cJSON* preset_item = cJSON_GetObjectItem(json, "preset");
  if (!preset_item || !cJSON_IsNumber(preset_item)) {
    WsReply(req, "set_kids_preset_ack", [](cJSON* reply) {
      cJSON_AddBoolToObject(reply, "ok", false);
      cJSON_AddStringToObject(reply, "error", "missing or invalid preset");
    });
    return;
  }

  int preset_val = preset_item->valueint;
  if (preset_val < 0 || preset_val > 3) {
    WsReply(req, "set_kids_preset_ack", [](cJSON* reply) {
      cJSON_AddBoolToObject(reply, "ok", false);
      cJSON_AddStringToObject(reply, "error", "preset out of range (0-3)");
    });
    return;
  }

  StabilizationConfig cfg = vc.GetStabilizationConfig();
  cfg.kids_mode.ApplyPreset(static_cast<KidsPreset>(preset_val));
  bool ok = vc.SetStabilizationConfig(cfg, true);

  const auto& applied = vc.GetStabilizationConfig();
  JsonDoc reply(ok ? StabilizationConfigToJson(applied) : cJSON_CreateObject());
  WsReply(req, "set_kids_preset_ack", std::move(reply),
          [&](cJSON* r) { cJSON_AddBoolToObject(r, "ok", ok); });

  ESP_LOGI(TAG, "set_kids_preset preset=%d -> %s", preset_val,
           ok ? "OK" : "FAILED");
}

void HandleToggleKidsMode(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  bool active = JsonGetBool(json, "active", false);
  vc.SetKidsModeActive(active);

  WsReply(req, "toggle_kids_mode_ack", [&](cJSON* reply) {
    cJSON_AddBoolToObject(reply, "active", active);
  });

  ESP_LOGI(TAG, "toggle_kids_mode active=%s -> OK", active ? "true" : "false");
}

void HandleGetKidsPresets(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  (void)vc;
  (void)json;

  WsReply(req, "kids_presets", [](cJSON* reply) {
    cJSON* presets = cJSON_CreateArray();
    if (!presets) return;

    // Единый источник истины — таблица пресетов в stabilization_config.
    // Числа определены в одном месте, UI не расходится с ApplyPreset().
    for (const KidsPresetInfo& info : GetKidsPresetTable()) {
      cJSON* item = cJSON_CreateObject();
      if (!item) continue;
      cJSON_AddNumberToObject(item, "id", static_cast<int>(info.id));
      cJSON_AddStringToObject(item, "name", info.name);
      cJSON_AddStringToObject(item, "description", info.description);
      // Custom не имеет фиксированных лимитов (NaN) — поля опускаем.
      if (!std::isnan(info.throttle_limit)) {
        cJSON_AddNumberToObject(item, "throttle_limit", info.throttle_limit);
        cJSON_AddNumberToObject(item, "steering_limit", info.steering_limit);
      }
      cJSON_AddItemToArray(presets, item);
    }

    cJSON_AddItemToObject(reply, "presets", presets);
  });
}

void HandleCalibrateSteeringTrim(IVehicleControl& vc, cJSON* json,
                                 httpd_req_t* req) {
  float target_accel = JsonGetFloat(json, "target_accel", 0.1f);

  bool ok = vc.StartSteeringTrimCalibration(target_accel);

  WsReply(req, "calibrate_steering_trim_ack", [&](cJSON* reply) {
    cJSON_AddBoolToObject(reply, "ok", ok);
    cJSON_AddStringToObject(reply, "status", ok ? "started" : "failed");
    if (!ok) {
      cJSON_AddStringToObject(
          reply, "error",
          "IMU not ready, another calibration active, or already running");
    }
    cJSON_AddNumberToObject(reply, "target_accel", target_accel);
  });

  ESP_LOGI(TAG, "calibrate_steering_trim target_accel=%.3fg -> %s",
           target_accel, ok ? "started" : "failed");
}

void HandleGetSteeringTrimStatus(IVehicleControl& vc, cJSON* json,
                                 httpd_req_t* req) {
  (void)json;

  bool active = vc.IsSteeringTrimCalibActive();
  auto result = vc.GetSteeringTrimCalibResult();

  WsReply(req, "steering_trim_status", [&](cJSON* reply) {
    cJSON_AddBoolToObject(reply, "active", active);

    cJSON* res = cJSON_CreateObject();
    if (res) {
      cJSON_AddBoolToObject(res, "valid", result.valid);
      cJSON_AddNumberToObject(res, "trim", result.trim);
      cJSON_AddNumberToObject(res, "mean_yaw_rate", result.mean_yaw_rate);
      cJSON_AddNumberToObject(res, "samples", result.samples);
      cJSON_AddItemToObject(reply, "result", res);
    }
  });
}

void HandleCalibrateComOffset(IVehicleControl& vc, cJSON* json,
                              httpd_req_t* req) {
  float target_accel = JsonGetFloat(json, "target_accel", 0.1f);
  float steering = JsonGetFloat(json, "steering", 0.5f);
  float duration = JsonGetFloat(json, "duration", 5.0f);

  bool ok = vc.StartComOffsetCalibration(target_accel, steering, duration);

  WsReply(req, "calibrate_com_offset_ack", [&](cJSON* reply) {
    cJSON_AddBoolToObject(reply, "ok", ok);
    cJSON_AddStringToObject(reply, "status", ok ? "started" : "failed");
    if (!ok) {
      cJSON_AddStringToObject(
          reply, "error",
          "IMU not ready, another procedure active, or already running");
    }
  });

  ESP_LOGI(TAG, "calibrate_com_offset accel=%.3fg steer=%.2f dur=%.1fs -> %s",
           target_accel, steering, duration, ok ? "started" : "failed");
}

void HandleGetComOffsetStatus(IVehicleControl& vc, cJSON* json,
                              httpd_req_t* req) {
  (void)json;

  bool active = vc.IsComOffsetCalibActive();
  auto result = vc.GetComOffsetCalibResult();

  WsReply(req, "com_offset_status", [&](cJSON* reply) {
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
  });
}

void HandleStartTest(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  TestParams params;

  const char* t = JsonGetString(json, "test_type", nullptr);
  if (t) {
    if (strcmp(t, "circle") == 0)
      params.type = TestType::Circle;
    else if (strcmp(t, "step") == 0)
      params.type = TestType::Step;
    else
      params.type = TestType::Straight;
  }

  params.target_accel_g =
      JsonGetFloat(json, "target_accel", params.target_accel_g);
  params.duration_sec = JsonGetFloat(json, "duration", params.duration_sec);
  params.steering = JsonGetFloat(json, "steering", params.steering);

  bool ok = vc.StartTest(params);

  WsReply(req, "start_test_ack", [&](cJSON* reply) {
    cJSON_AddBoolToObject(reply, "ok", ok);
    const char* type_str = "straight";
    if (params.type == TestType::Circle)
      type_str = "circle";
    else if (params.type == TestType::Step)
      type_str = "step";
    cJSON_AddStringToObject(reply, "test_type", type_str);
    if (!ok) {
      cJSON_AddStringToObject(
          reply, "error",
          "RC transmitter active, IMU not ready, another procedure active, "
          "or test already running");
    }
  });

  ESP_LOGI(TAG, "start_test type=%s accel=%.3fg dur=%.1fs steer=%.2f -> %s",
           params.type == TestType::Circle ? "circle"
           : params.type == TestType::Step ? "step"
                                           : "straight",
           params.target_accel_g, params.duration_sec, params.steering,
           ok ? "started" : "failed");
}

void HandleStopTest(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  (void)json;
  vc.StopTest();

  WsReply(req, "stop_test_ack",
          [](cJSON* reply) { cJSON_AddBoolToObject(reply, "ok", true); });

  ESP_LOGI(TAG, "stop_test");
}

void HandleGetTestStatus(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  (void)json;

  bool active = vc.IsTestActive();
  auto status = vc.GetTestStatus();

  WsReply(req, "test_status", [&](cJSON* reply) {
    cJSON_AddBoolToObject(reply, "active", active);

    const char* phase_str = "idle";
    switch (status.phase) {
      case TestRunner::Phase::Accelerate:
        phase_str = "accelerate";
        break;
      case TestRunner::Phase::Cruise:
        phase_str = "cruise";
        break;
      case TestRunner::Phase::StepExec:
        phase_str = "step_exec";
        break;
      case TestRunner::Phase::Brake:
        phase_str = "brake";
        break;
      case TestRunner::Phase::Done:
        phase_str = "done";
        break;
      case TestRunner::Phase::Failed:
        phase_str = "failed";
        break;
      default:
        break;
    }
    cJSON_AddStringToObject(reply, "phase", phase_str);

    const char* type_str = "straight";
    if (status.type == TestType::Circle)
      type_str = "circle";
    else if (status.type == TestType::Step)
      type_str = "step";
    cJSON_AddStringToObject(reply, "test_type", type_str);

    cJSON_AddNumberToObject(reply, "elapsed", status.elapsed_sec);
    cJSON_AddNumberToObject(reply, "phase_elapsed", status.phase_elapsed_sec);
    cJSON_AddBoolToObject(reply, "valid", status.valid);
  });
}

void HandleStartSpeedCalib(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  float throttle = JsonGetFloat(json, "throttle", 0.3f);
  float duration = JsonGetFloat(json, "duration", 3.0f);

  bool ok = vc.StartSpeedCalibration(throttle, duration);

  WsReply(req, "start_speed_calib_ack", [&](cJSON* reply) {
    cJSON_AddBoolToObject(reply, "ok", ok);
    cJSON_AddStringToObject(reply, "status", ok ? "started" : "failed");
    if (!ok) {
      cJSON_AddStringToObject(
          reply, "error",
          "IMU not ready, another procedure active, or already running");
    }
    cJSON_AddNumberToObject(reply, "throttle", throttle);
    cJSON_AddNumberToObject(reply, "duration", duration);
  });

  ESP_LOGI(TAG, "start_speed_calib throttle=%.2f dur=%.1fs -> %s", throttle,
           duration, ok ? "started" : "failed");
}

void HandleStopSpeedCalib(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  (void)json;
  vc.StopSpeedCalibration();

  WsReply(req, "stop_speed_calib_ack",
          [](cJSON* reply) { cJSON_AddBoolToObject(reply, "ok", true); });

  ESP_LOGI(TAG, "stop_speed_calib");
}

void HandleGetSpeedCalibStatus(IVehicleControl& vc, cJSON* json,
                               httpd_req_t* req) {
  (void)json;

  bool active = vc.IsSpeedCalibActive();
  auto result = vc.GetSpeedCalibResult();

  WsReply(req, "speed_calib_status", [&](cJSON* reply) {
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
  });
}

void HandleRunSelfTest(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  (void)json;

  auto results = vc.RunSelfTest();
  bool all_passed = rc_vehicle::SelfTest::AllPassed(results);

  WsReply(req, "self_test_result", [&](cJSON* reply) {
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
  });

  // Разбивка по проверкам в serial-лог: без неё в boot-логе виден только итог
  // (см. LOS-220). Проваленные — WARN, прошедшие — INFO (esp32_s3 sdkconfig
  // ограничивает CONFIG_LOG_MAXIMUM_LEVEL уровнем INFO, поэтому ESP_LOGD здесь
  // компилируется в no-op и строки PASS были бы не видны).
  for (const auto& item : results) {
    if (item.passed) {
      ESP_LOGI(TAG, "  self_test ok:   %s = %s", item.name, item.value);
    } else {
      ESP_LOGW(TAG, "  self_test FAIL: %s = %s", item.name, item.value);
    }
  }

  size_t failed = rc_vehicle::SelfTest::FailedCount(results);
  ESP_LOGI(TAG, "run_self_test -> %s (%zu/%zu failed)",
           all_passed ? "ALL PASS" : "FAIL", failed, results.size());
}

void HandleUdpStreamStart(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  (void)vc;
  const char* ip = JsonGetString(json, "ip", nullptr);
  // Валидируем в int ДО каста: иначе hz=266 молча усекается до 10
  // (uint8_t) и is_valid_hz() его принимает — клиент получает не то, что
  // просил, без ошибки. port [1024,65535], hz [0,255] (uint8_t).
  bool params_ok = true;
  int port_i = JsonGetIntChecked(json, "port", 5555, 1024, 65535, &params_ok);
  int hz_i = JsonGetIntChecked(json, "hz", 100, 0, 255, &params_ok);
  uint16_t port = (uint16_t)port_i;
  uint8_t hz = (uint8_t)hz_i;

  WsReply(req, "udp_stream_start_ack", [&](cJSON* reply) {
    if (!ip) {
      cJSON_AddBoolToObject(reply, "ok", false);
      cJSON_AddStringToObject(reply, "error", "missing ip field");
      return;
    }
    if (!params_ok) {
      cJSON_AddBoolToObject(reply, "ok", false);
      cJSON_AddStringToObject(reply, "error",
                              "port out of [1024,65535] or hz out of [0,255]");
      return;
    }
    bool ok = UdpTelemStart(ip, port, hz);
    cJSON_AddBoolToObject(reply, "ok", ok);
    if (ok) {
      cJSON_AddStringToObject(reply, "ip", ip);
      cJSON_AddNumberToObject(reply, "port", port);
      cJSON_AddNumberToObject(reply, "hz", hz);
    } else {
      cJSON_AddStringToObject(reply, "error", "invalid parameters");
    }
  });

  ESP_LOGI(TAG, "udp_stream_start ip=%s port=%u hz=%u ok=%d", ip ? ip : "null",
           port, hz, (int)params_ok);
}

void HandleUdpStreamStop(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  (void)vc;
  (void)json;
  UdpTelemStop();

  WsReply(req, "udp_stream_stop_ack",
          [](cJSON* reply) { cJSON_AddBoolToObject(reply, "ok", true); });

  ESP_LOGI(TAG, "udp_stream_stop");
}

void HandleUdpStreamStatus(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  (void)vc;
  (void)json;

  WsReply(req, "udp_stream_status", [](cJSON* reply) {
    cJSON_AddBoolToObject(reply, "streaming", UdpTelemIsStreaming());
    cJSON_AddStringToObject(reply, "ip", UdpTelemGetTargetIp());
    cJSON_AddNumberToObject(reply, "port", UdpTelemGetTargetPort());
    cJSON_AddNumberToObject(reply, "hz", UdpTelemGetHz());
    cJSON_AddNumberToObject(reply, "seq", (double)UdpTelemGetSeq());
    cJSON_AddNumberToObject(reply, "dropped", (double)UdpTelemGetDropped());
  });
}

void HandleCalibrateMag(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  const char* action = JsonGetString(json, "action", "");

  bool ok = true;
  // Все четыре действия откладываются на control loop (ревью PR #308):
  // применятся первым же тиком (≤2 мс), а статус в ack ниже на этот момент
  // ещё прежний. UI и так опрашивает get_mag_calib_status.
  //
  // ok здесь — факт ПРИЁМА команды, а не результат операции: false означает
  // переполненную очередь. Молчать об этом нельзя, иначе отброшенный cancel
  // оставит калибровку собирать семплы вечно при том, что клиент считает
  // команду принятой.
  if (strcmp(action, "start") == 0) {
    ok = vc.StartMagCalibration();
    ESP_LOGI(TAG, "calibrate_mag: start %s", ok ? "queued" : "DROPPED (full)");
  } else if (strcmp(action, "finish") == 0) {
    ok = vc.FinishMagCalibration();
    ESP_LOGI(TAG, "calibrate_mag: finish %s", ok ? "queued" : "DROPPED (full)");
  } else if (strcmp(action, "cancel") == 0) {
    ok = vc.CancelMagCalibration();
    ESP_LOGI(TAG, "calibrate_mag: cancel %s", ok ? "queued" : "DROPPED (full)");
  } else if (strcmp(action, "erase") == 0) {
    ok = vc.EraseMagCalibration();
    ESP_LOGI(TAG, "calibrate_mag: erase %s", ok ? "queued" : "DROPPED (full)");
  } else {
    ok = false;
    ESP_LOGW(TAG, "calibrate_mag: unknown action '%s'", action);
  }

  const MagCalibStateView mag_state = vc.GetMagCalibState();
  WsReply(req, "calibrate_mag_ack", [&](cJSON* reply) {
    cJSON_AddStringToObject(reply, "status", mag_state.status);
    cJSON_AddStringToObject(reply, "fail_reason", mag_state.fail_reason);
    cJSON_AddBoolToObject(reply, "ok", ok);
  });
}

void HandleGetMagCalibStatus(IVehicleControl& vc, cJSON* json,
                             httpd_req_t* req) {
  (void)json;

  const MagCalibStateView mag_state = vc.GetMagCalibState();
  WsReply(req, "mag_calib_status", [&](cJSON* reply) {
    cJSON_AddStringToObject(reply, "status", mag_state.status);
    cJSON_AddStringToObject(reply, "fail_reason", mag_state.fail_reason);
  });
}

void HandleResetHeadingRef(IVehicleControl& vc, cJSON* json, httpd_req_t* req) {
  (void)json;
  vc.ResetHeadingRef();

  WsReply(req, "reset_heading_ref_ack", [](cJSON* reply) {
    cJSON_AddBoolToObject(reply, "ok", true);
    cJSON_AddStringToObject(reply, "status", "heading ref reset");
  });

  ESP_LOGI(TAG, "reset_heading_ref");
}

}  // namespace rc_vehicle
