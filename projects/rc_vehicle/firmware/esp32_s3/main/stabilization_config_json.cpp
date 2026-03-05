#include "stabilization_config_json.hpp"

using rc_vehicle::DriveMode;
using rc_vehicle::KidsPreset;
using rc_vehicle::StabilizationConfig;

cJSON* StabilizationConfigToJson(const StabilizationConfig& cfg) {
  cJSON* obj = cJSON_CreateObject();
  if (!obj) return nullptr;

  // Общие параметры
  cJSON_AddBoolToObject(obj, "enabled", cfg.enabled);
  cJSON_AddNumberToObject(obj, "mode", static_cast<uint8_t>(cfg.mode));
  cJSON_AddNumberToObject(obj, "fade_ms", cfg.fade_ms);

  // Filter config
  cJSON* filter = cJSON_AddObjectToObject(obj, "filter");
  if (filter) {
    cJSON_AddNumberToObject(filter, "madgwick_beta", cfg.filter.madgwick_beta);
    cJSON_AddNumberToObject(filter, "lpf_cutoff_hz", cfg.filter.lpf_cutoff_hz);
    cJSON_AddNumberToObject(filter, "imu_sample_rate_hz",
                            cfg.filter.imu_sample_rate_hz);
  }

  // Yaw rate config
  cJSON* yaw_rate = cJSON_AddObjectToObject(obj, "yaw_rate");
  if (yaw_rate) {
    cJSON* pid = cJSON_AddObjectToObject(yaw_rate, "pid");
    if (pid) {
      cJSON_AddNumberToObject(pid, "kp", cfg.yaw_rate.pid.kp);
      cJSON_AddNumberToObject(pid, "ki", cfg.yaw_rate.pid.ki);
      cJSON_AddNumberToObject(pid, "kd", cfg.yaw_rate.pid.kd);
      cJSON_AddNumberToObject(pid, "max_integral",
                              cfg.yaw_rate.pid.max_integral);
      cJSON_AddNumberToObject(pid, "max_correction",
                              cfg.yaw_rate.pid.max_correction);
    }
    cJSON_AddNumberToObject(yaw_rate, "steer_to_yaw_rate_dps",
                            cfg.yaw_rate.steer_to_yaw_rate_dps);
  }

  // Slip angle config
  cJSON* slip_angle = cJSON_AddObjectToObject(obj, "slip_angle");
  if (slip_angle) {
    cJSON* pid = cJSON_AddObjectToObject(slip_angle, "pid");
    if (pid) {
      cJSON_AddNumberToObject(pid, "kp", cfg.slip_angle.pid.kp);
      cJSON_AddNumberToObject(pid, "ki", cfg.slip_angle.pid.ki);
      cJSON_AddNumberToObject(pid, "kd", cfg.slip_angle.pid.kd);
      cJSON_AddNumberToObject(pid, "max_integral",
                              cfg.slip_angle.pid.max_integral);
      cJSON_AddNumberToObject(pid, "max_correction",
                              cfg.slip_angle.pid.max_correction);
    }
    cJSON_AddNumberToObject(slip_angle, "target_deg",
                            cfg.slip_angle.target_deg);
  }

  // Adaptive config
  cJSON* adaptive = cJSON_AddObjectToObject(obj, "adaptive");
  if (adaptive) {
    cJSON_AddBoolToObject(adaptive, "enabled", cfg.adaptive.enabled);
    cJSON_AddNumberToObject(adaptive, "speed_ref_ms",
                            cfg.adaptive.speed_ref_ms);
    cJSON_AddNumberToObject(adaptive, "scale_min", cfg.adaptive.scale_min);
    cJSON_AddNumberToObject(adaptive, "scale_max", cfg.adaptive.scale_max);
  }

  // Oversteer config
  cJSON* oversteer = cJSON_AddObjectToObject(obj, "oversteer");
  if (oversteer) {
    cJSON_AddBoolToObject(oversteer, "warn_enabled",
                          cfg.oversteer.warn_enabled);
    cJSON_AddNumberToObject(oversteer, "slip_thresh_deg",
                            cfg.oversteer.slip_thresh_deg);
    cJSON_AddNumberToObject(oversteer, "rate_thresh_deg_s",
                            cfg.oversteer.rate_thresh_deg_s);
    cJSON_AddNumberToObject(oversteer, "throttle_reduction",
                            cfg.oversteer.throttle_reduction);
  }

  // Pitch compensation config
  cJSON* pitch_comp = cJSON_AddObjectToObject(obj, "pitch_comp");
  if (pitch_comp) {
    cJSON_AddBoolToObject(pitch_comp, "enabled", cfg.pitch_comp.enabled);
    cJSON_AddNumberToObject(pitch_comp, "gain", cfg.pitch_comp.gain);
    cJSON_AddNumberToObject(pitch_comp, "max_correction",
                            cfg.pitch_comp.max_correction);
  }

  // Kids mode config
  cJSON* kids_mode = cJSON_AddObjectToObject(obj, "kids_mode");
  if (kids_mode) {
    cJSON_AddNumberToObject(kids_mode, "throttle_limit",
                            cfg.kids_mode.throttle_limit);
    cJSON_AddNumberToObject(kids_mode, "reverse_limit",
                            cfg.kids_mode.reverse_limit);
    cJSON_AddNumberToObject(kids_mode, "steering_limit",
                            cfg.kids_mode.steering_limit);
    cJSON_AddNumberToObject(kids_mode, "slew_throttle",
                            cfg.kids_mode.slew_throttle);
    cJSON_AddNumberToObject(kids_mode, "slew_steering",
                            cfg.kids_mode.slew_steering);
    cJSON_AddBoolToObject(kids_mode, "anti_spin_enabled",
                          cfg.kids_mode.anti_spin_enabled);
    cJSON_AddNumberToObject(kids_mode, "anti_spin_threshold_deg",
                            cfg.kids_mode.anti_spin_threshold_deg);
    cJSON_AddNumberToObject(kids_mode, "anti_spin_reduction",
                            cfg.kids_mode.anti_spin_reduction);
  }

  return obj;
}

void StabilizationConfigFromJson(StabilizationConfig& cfg, const cJSON* json) {
  auto get_bool = [](const cJSON* parent, const char* key, bool& field) {
    cJSON* item = cJSON_GetObjectItem(parent, key);
    if (item && cJSON_IsBool(item)) field = cJSON_IsTrue(item);
  };
  auto get_float = [](const cJSON* parent, const char* key, float& field) {
    cJSON* item = cJSON_GetObjectItem(parent, key);
    if (item && cJSON_IsNumber(item))
      field = static_cast<float>(item->valuedouble);
  };
  auto get_drive_mode = [](const cJSON* parent, const char* key,
                           DriveMode& field) {
    cJSON* item = cJSON_GetObjectItem(parent, key);
    if (item && cJSON_IsNumber(item))
      field = static_cast<DriveMode>(item->valueint);
  };
  auto get_uint32 = [](const cJSON* parent, const char* key, uint32_t& field) {
    cJSON* item = cJSON_GetObjectItem(parent, key);
    if (item && cJSON_IsNumber(item))
      field = static_cast<uint32_t>(item->valueint);
  };

  // Общие параметры
  get_bool(json, "enabled", cfg.enabled);
  get_drive_mode(json, "mode", cfg.mode);
  get_uint32(json, "fade_ms", cfg.fade_ms);

  // Filter config
  cJSON* filter = cJSON_GetObjectItem(json, "filter");
  if (filter) {
    get_float(filter, "madgwick_beta", cfg.filter.madgwick_beta);
    get_float(filter, "lpf_cutoff_hz", cfg.filter.lpf_cutoff_hz);
    get_float(filter, "imu_sample_rate_hz", cfg.filter.imu_sample_rate_hz);
  }

  // Yaw rate config
  cJSON* yaw_rate = cJSON_GetObjectItem(json, "yaw_rate");
  if (yaw_rate) {
    cJSON* pid = cJSON_GetObjectItem(yaw_rate, "pid");
    if (pid) {
      get_float(pid, "kp", cfg.yaw_rate.pid.kp);
      get_float(pid, "ki", cfg.yaw_rate.pid.ki);
      get_float(pid, "kd", cfg.yaw_rate.pid.kd);
      get_float(pid, "max_integral", cfg.yaw_rate.pid.max_integral);
      get_float(pid, "max_correction", cfg.yaw_rate.pid.max_correction);
    }
    get_float(yaw_rate, "steer_to_yaw_rate_dps",
              cfg.yaw_rate.steer_to_yaw_rate_dps);
  }

  // Slip angle config
  cJSON* slip_angle = cJSON_GetObjectItem(json, "slip_angle");
  if (slip_angle) {
    cJSON* pid = cJSON_GetObjectItem(slip_angle, "pid");
    if (pid) {
      get_float(pid, "kp", cfg.slip_angle.pid.kp);
      get_float(pid, "ki", cfg.slip_angle.pid.ki);
      get_float(pid, "kd", cfg.slip_angle.pid.kd);
      get_float(pid, "max_integral", cfg.slip_angle.pid.max_integral);
      get_float(pid, "max_correction", cfg.slip_angle.pid.max_correction);
    }
    get_float(slip_angle, "target_deg", cfg.slip_angle.target_deg);
  }

  // Adaptive config
  cJSON* adaptive = cJSON_GetObjectItem(json, "adaptive");
  if (adaptive) {
    get_bool(adaptive, "enabled", cfg.adaptive.enabled);
    get_float(adaptive, "speed_ref_ms", cfg.adaptive.speed_ref_ms);
    get_float(adaptive, "scale_min", cfg.adaptive.scale_min);
    get_float(adaptive, "scale_max", cfg.adaptive.scale_max);
  }

  // Oversteer config
  cJSON* oversteer = cJSON_GetObjectItem(json, "oversteer");
  if (oversteer) {
    get_bool(oversteer, "warn_enabled", cfg.oversteer.warn_enabled);
    get_float(oversteer, "slip_thresh_deg", cfg.oversteer.slip_thresh_deg);
    get_float(oversteer, "rate_thresh_deg_s", cfg.oversteer.rate_thresh_deg_s);
    get_float(oversteer, "throttle_reduction",
              cfg.oversteer.throttle_reduction);
  }

  // Pitch compensation config
  cJSON* pitch_comp = cJSON_GetObjectItem(json, "pitch_comp");
  if (pitch_comp) {
    get_bool(pitch_comp, "enabled", cfg.pitch_comp.enabled);
    get_float(pitch_comp, "gain", cfg.pitch_comp.gain);
    get_float(pitch_comp, "max_correction", cfg.pitch_comp.max_correction);
  }

  // Kids mode config
  cJSON* kids_mode = cJSON_GetObjectItem(json, "kids_mode");
  if (kids_mode) {
    get_float(kids_mode, "throttle_limit", cfg.kids_mode.throttle_limit);
    get_float(kids_mode, "reverse_limit", cfg.kids_mode.reverse_limit);
    get_float(kids_mode, "steering_limit", cfg.kids_mode.steering_limit);
    get_float(kids_mode, "slew_throttle", cfg.kids_mode.slew_throttle);
    get_float(kids_mode, "slew_steering", cfg.kids_mode.slew_steering);
    get_bool(kids_mode, "anti_spin_enabled", cfg.kids_mode.anti_spin_enabled);
    get_float(kids_mode, "anti_spin_threshold_deg",
              cfg.kids_mode.anti_spin_threshold_deg);
    get_float(kids_mode, "anti_spin_reduction",
              cfg.kids_mode.anti_spin_reduction);
  }
}
