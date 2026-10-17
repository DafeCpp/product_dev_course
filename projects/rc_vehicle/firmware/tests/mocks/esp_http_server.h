#pragma once

// Mock of the ESP-IDF esp_http_server.h subset used by ws_command_registry.cpp
// (LOS-37). Host-side GTests only: httpd_ws_send_frame() records the frames
// instead of writing to a socket, and the result code can be forced to test
// the error path.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_FAIL = -1;

enum httpd_ws_type_t {
  HTTPD_WS_TYPE_CONTINUE = 0x0,
  HTTPD_WS_TYPE_TEXT = 0x1,
  HTTPD_WS_TYPE_BINARY = 0x2,
  HTTPD_WS_TYPE_CLOSE = 0x8,
  HTTPD_WS_TYPE_PING = 0x9,
  HTTPD_WS_TYPE_PONG = 0xA,
};

struct httpd_req_t {
  int unused{0};
};

struct httpd_ws_frame_t {
  bool final;
  bool fragmented;
  httpd_ws_type_t type;
  uint8_t* payload;
  size_t len;
};

namespace http_server_mock {

/** Копия отправленного кадра: payload владеет строкой, а не указателем. */
struct SentFrame {
  const httpd_req_t* req{nullptr};
  bool final{false};
  bool fragmented{false};
  httpd_ws_type_t type{HTTPD_WS_TYPE_CONTINUE};
  std::string payload;
};

struct State {
  std::vector<SentFrame> frames;
  esp_err_t send_result{ESP_OK};
  int send_calls{0};
};

inline State& GetState() {
  static State state;
  return state;
}

/** Сбросить кадры и результат отправки; вызывать в SetUp() каждого теста. */
inline void Reset() { GetState() = State{}; }

}  // namespace http_server_mock

inline esp_err_t httpd_ws_send_frame(httpd_req_t* req, httpd_ws_frame_t* pkt) {
  auto& state = http_server_mock::GetState();
  ++state.send_calls;
  if (pkt != nullptr) {
    http_server_mock::SentFrame frame;
    frame.req = req;
    frame.final = pkt->final;
    frame.fragmented = pkt->fragmented;
    frame.type = pkt->type;
    if (pkt->payload != nullptr) {
      frame.payload.assign(reinterpret_cast<const char*>(pkt->payload),
                           pkt->len);
    }
    state.frames.push_back(std::move(frame));
  }
  return state.send_result;
}

inline const char* esp_err_to_name(esp_err_t err) {
  return err == ESP_OK ? "ESP_OK" : "ESP_FAIL";
}
