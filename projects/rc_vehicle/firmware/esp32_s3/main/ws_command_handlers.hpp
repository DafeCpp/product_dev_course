#pragma once

#include "cJSON.h"
#include "esp_http_server.h"

namespace rc_vehicle {

/**
 * @brief WebSocket command handlers
 *
 * These functions handle specific WebSocket JSON commands.
 * Each handler is responsible for:
 * 1. Parsing command-specific parameters from JSON
 * 2. Executing the command logic
 * 3. Sending a JSON response back to the client
 */

/**
 * @brief Handle IMU calibration command
 *
 * Supports three modes:
 * - "gyro": Calibrate gyroscope only
 * - "full": Full calibration (stage 1: stationary)
 * - "forward": Forward calibration (stage 2: moving forward)
 *
 * Request: {"type":"calibrate_imu","mode":"gyro"|"full"|"forward"}
 * Response:
 * {"type":"calibrate_imu_ack","status":"collecting"|"failed","stage":1|2,"ok":true|false}
 */
void HandleCalibrateImu(cJSON* json, httpd_req_t* req);

/**
 * @brief Get calibration status
 *
 * Request: {"type":"get_calib_status"}
 * Response: {"type":"calib_status","status":"...","stage":1|2}
 */
void HandleGetCalibStatus(cJSON* json, httpd_req_t* req);

/**
 * @brief Set forward direction vector
 *
 * Request: {"type":"set_forward_direction","vec":[fx,fy,fz]}
 * Response: {"type":"set_forward_direction_ack","ok":true}
 */
void HandleSetForwardDirection(cJSON* json, httpd_req_t* req);

/**
 * @brief Get stabilization configuration
 *
 * Request: {"type":"get_stab_config"}
 * Response: {"type":"stab_config",...config fields...}
 */
void HandleGetStabConfig(cJSON* json, httpd_req_t* req);

/**
 * @brief Set stabilization configuration
 *
 * Request: {"type":"set_stab_config",...config fields...}
 * Response: {"type":"set_stab_config_ack","ok":true,...applied config...}
 */
void HandleSetStabConfig(cJSON* json, httpd_req_t* req);

/**
 * @brief Get telemetry log information
 *
 * Request: {"type":"get_log_info"}
 * Response: {"type":"log_info","count":N,"capacity":M}
 */
void HandleGetLogInfo(cJSON* json, httpd_req_t* req);

/**
 * @brief Get telemetry log data
 *
 * Request: {"type":"get_log_data","offset":N,"count":M}
 * Response: {"type":"log_data","frames":[...]}
 */
void HandleGetLogData(cJSON* json, httpd_req_t* req);

/**
 * @brief Clear telemetry log
 *
 * Request: {"type":"clear_log"}
 * Response: {"type":"clear_log_ack","ok":true}
 */
void HandleClearLog(cJSON* json, httpd_req_t* req);

}  // namespace rc_vehicle