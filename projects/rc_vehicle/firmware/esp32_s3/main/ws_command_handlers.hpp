#pragma once

#include "cJSON.h"
#include "esp_http_server.h"

namespace rc_vehicle {

class IVehicleControl;

/**
 * @brief WebSocket command handlers
 *
 * All handlers receive an IVehicleControl& via dependency injection
 * instead of accessing the global singleton directly.
 */

/**
 * @brief Команда управления throttle/steering (тип "cmd").
 *
 * Раньше обрабатывалась в websocket_server отдельным колбэком
 * (throttle, steering); после развязки websocket_server от rc_vehicle идёт
 * через тот же registry, что и остальные JSON-команды.
 */
void HandleControlCmd(IVehicleControl& vc, cJSON* json, httpd_req_t* req);

void HandleCalibrateImu(IVehicleControl& vc, cJSON* json, httpd_req_t* req);
void HandleGetCalibStatus(IVehicleControl& vc, cJSON* json, httpd_req_t* req);
void HandleSetForwardDirection(IVehicleControl& vc, cJSON* json,
                               httpd_req_t* req);
void HandleGetStabConfig(IVehicleControl& vc, cJSON* json, httpd_req_t* req);
void HandleSetStabConfig(IVehicleControl& vc, cJSON* json, httpd_req_t* req);
void HandleGetLogInfo(IVehicleControl& vc, cJSON* json, httpd_req_t* req);
void HandleGetLogData(IVehicleControl& vc, cJSON* json, httpd_req_t* req);
void HandleClearLog(IVehicleControl& vc, cJSON* json, httpd_req_t* req);
void HandleSetKidsPreset(IVehicleControl& vc, cJSON* json, httpd_req_t* req);
void HandleGetKidsPresets(IVehicleControl& vc, cJSON* json, httpd_req_t* req);
void HandleToggleKidsMode(IVehicleControl& vc, cJSON* json, httpd_req_t* req);
void HandleCalibrateSteeringTrim(IVehicleControl& vc, cJSON* json,
                                 httpd_req_t* req);
void HandleGetSteeringTrimStatus(IVehicleControl& vc, cJSON* json,
                                 httpd_req_t* req);
void HandleCalibrateComOffset(IVehicleControl& vc, cJSON* json,
                              httpd_req_t* req);
void HandleGetComOffsetStatus(IVehicleControl& vc, cJSON* json,
                              httpd_req_t* req);
void HandleStartTest(IVehicleControl& vc, cJSON* json, httpd_req_t* req);
void HandleStopTest(IVehicleControl& vc, cJSON* json, httpd_req_t* req);
void HandleGetTestStatus(IVehicleControl& vc, cJSON* json, httpd_req_t* req);
void HandleStartSpeedCalib(IVehicleControl& vc, cJSON* json, httpd_req_t* req);
void HandleStopSpeedCalib(IVehicleControl& vc, cJSON* json, httpd_req_t* req);
void HandleGetSpeedCalibStatus(IVehicleControl& vc, cJSON* json,
                               httpd_req_t* req);
void HandleRunSelfTest(IVehicleControl& vc, cJSON* json, httpd_req_t* req);
void HandleUdpStreamStart(IVehicleControl& vc, cJSON* json, httpd_req_t* req);
void HandleUdpStreamStop(IVehicleControl& vc, cJSON* json, httpd_req_t* req);
void HandleUdpStreamStatus(IVehicleControl& vc, cJSON* json, httpd_req_t* req);
void HandleCalibrateMag(IVehicleControl& vc, cJSON* json, httpd_req_t* req);
void HandleGetMagCalibStatus(IVehicleControl& vc, cJSON* json,
                             httpd_req_t* req);
void HandleResetHeadingRef(IVehicleControl& vc, cJSON* json, httpd_req_t* req);

}  // namespace rc_vehicle
