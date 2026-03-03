#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include "cJSON.h"
#include "esp_http_server.h"

namespace rc_vehicle {

/**
 * @brief Handler function type for WebSocket JSON commands
 *
 * @param json The parsed JSON object containing the command
 * @param req The HTTP request handle for sending responses
 */
using WsJsonHandler = std::function<void(cJSON* json, httpd_req_t* req)>;

/**
 * @brief Registry for WebSocket JSON command handlers
 *
 * Implements the Command pattern to handle different WebSocket JSON commands.
 * This replaces the large if-else chain with a more extensible registry-based
 * approach.
 *
 * Example usage:
 * @code
 * WsCommandRegistry registry;
 * registry.Register("calibrate_imu", HandleCalibrateImu);
 * registry.Register("get_stab_config", HandleGetStabConfig);
 *
 * // In WebSocket handler:
 * registry.Handle(command_type, json, req);
 * @endcode
 */
class WsCommandRegistry {
 public:
  WsCommandRegistry() = default;
  ~WsCommandRegistry() = default;

  // Non-copyable, non-movable (contains function pointers)
  WsCommandRegistry(const WsCommandRegistry&) = delete;
  WsCommandRegistry& operator=(const WsCommandRegistry&) = delete;
  WsCommandRegistry(WsCommandRegistry&&) = delete;
  WsCommandRegistry& operator=(WsCommandRegistry&&) = delete;

  /**
   * @brief Register a handler for a specific command type
   *
   * @param type Command type string (e.g., "calibrate_imu")
   * @param handler Function to handle this command type
   */
  void Register(const std::string& type, WsJsonHandler handler);

  /**
   * @brief Handle a command by dispatching to the registered handler
   *
   * @param type Command type string
   * @param json Parsed JSON object
   * @param req HTTP request handle
   * @return true if handler was found and executed, false otherwise
   */
  bool Handle(const char* type, cJSON* json, httpd_req_t* req);

  /**
   * @brief Check if a handler is registered for a command type
   *
   * @param type Command type string
   * @return true if handler exists, false otherwise
   */
  bool HasHandler(const char* type) const;

  /**
   * @brief Get the number of registered handlers
   *
   * @return Number of registered command handlers
   */
  size_t GetHandlerCount() const { return handlers_.size(); }

 private:
  std::unordered_map<std::string, WsJsonHandler> handlers_;
};

/**
 * @brief Utility function to send a JSON reply via WebSocket
 *
 * @param req HTTP request handle
 * @param reply JSON object to send (will be freed by caller)
 */
void WsSendJsonReply(httpd_req_t* req, cJSON* reply);

}  // namespace rc_vehicle