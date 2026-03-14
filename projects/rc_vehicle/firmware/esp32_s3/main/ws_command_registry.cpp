#include "ws_command_registry.hpp"

#include <cstring>

#include "esp_log.h"
#include "vehicle_control.hpp"

static const char* TAG = "ws_cmd_registry";

namespace rc_vehicle {

void WsCommandRegistry::Register(const std::string& type,
                                 WsJsonHandler handler) {
  if (handler) {
    handlers_[type] = std::move(handler);
    ESP_LOGI(TAG, "Registered handler for command type: %s", type.c_str());
  } else {
    ESP_LOGW(TAG, "Attempted to register null handler for type: %s",
             type.c_str());
  }
}

bool WsCommandRegistry::Handle(const char* type, cJSON* json,
                               httpd_req_t* req) {
  if (!type) {
    ESP_LOGW(TAG, "Handle called with null type");
    return false;
  }

  // Init-ready barrier: отклонить команды до готовности control task
  if (!VehicleControlIsReady()) {
    ESP_LOGW(TAG, "Command '%s' rejected: control task not ready", type);
    cJSON* reply = cJSON_CreateObject();
    if (reply) {
      cJSON_AddStringToObject(reply, "type", "error");
      cJSON_AddStringToObject(reply, "error", "not_ready");
      cJSON_AddStringToObject(reply, "message",
                              "Control task is still initializing");
      WsSendJsonReply(req, reply);
      cJSON_Delete(reply);
    }
    return true;  // Команда обработана (отклонена)
  }

  auto it = handlers_.find(type);
  if (it != handlers_.end()) {
    ESP_LOGD(TAG, "Handling command: %s", type);
    it->second(json, req);
    return true;
  }

  ESP_LOGW(TAG, "No handler registered for command type: %s", type);
  return false;
}

bool WsCommandRegistry::HasHandler(const char* type) const {
  if (!type) {
    return false;
  }
  return handlers_.find(type) != handlers_.end();
}

void WsSendJsonReply(httpd_req_t* req, cJSON* reply) {
  if (!req || !reply) {
    ESP_LOGW(TAG, "WsSendJsonReply called with null argument");
    return;
  }

  char* str = cJSON_PrintUnformatted(reply);
  if (str) {
    httpd_ws_frame_t pkt = {};
    pkt.final = true;
    pkt.fragmented = false;
    pkt.type = HTTPD_WS_TYPE_TEXT;
    pkt.payload = reinterpret_cast<uint8_t*>(str);
    pkt.len = strlen(str);

    esp_err_t ret = httpd_ws_send_frame(req, &pkt);
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "Failed to send WebSocket frame: %s", esp_err_to_name(ret));
    }

    free(str);
  } else {
    ESP_LOGE(TAG, "Failed to serialize JSON reply");
  }
}

}  // namespace rc_vehicle