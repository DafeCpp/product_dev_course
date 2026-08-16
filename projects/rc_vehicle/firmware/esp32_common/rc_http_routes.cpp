#include "rc_http_routes.hpp"

#include "crash_logger.hpp"
#include "esp_http_server.h"
#include "esp_log.h"
#include "telemetry_config_snapshot.hpp"
#include "telemetry_event_log.hpp"
#include "telemetry_log.hpp"
#include "vehicle_control.hpp"

static const char* TAG = "rc_http_routes";

// Веб-ресурсы (HTML/CSS/JS) вшиваются в прошивку через #embed.
// Требование: GCC с поддержкой C23 #embed (обычно GCC 15+).
static const unsigned char INDEX_HTML[] = {
#embed "web/index.html" suffix(, 0) if_empty(0)
};
static constexpr size_t INDEX_HTML_LEN = sizeof(INDEX_HTML) - 1;

static const unsigned char STYLE_CSS[] = {
#embed "web/style.css" suffix(, 0) if_empty(0)
};
static constexpr size_t STYLE_CSS_LEN = sizeof(STYLE_CSS) - 1;

static const unsigned char APP_JS[] = {
#embed "web/app.js" suffix(, 0) if_empty(0)
};
static constexpr size_t APP_JS_LEN = sizeof(APP_JS) - 1;

static esp_err_t root_get_handler(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  httpd_resp_send(req, reinterpret_cast<const char*>(INDEX_HTML),
                  INDEX_HTML_LEN);
  return ESP_OK;
}

static esp_err_t style_css_handler(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/css");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  httpd_resp_send(req, reinterpret_cast<const char*>(STYLE_CSS), STYLE_CSS_LEN);
  return ESP_OK;
}

static esp_err_t app_js_handler(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/javascript");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  httpd_resp_send(req, reinterpret_cast<const char*>(APP_JS), APP_JS_LEN);
  return ESP_OK;
}

// ─────────────────────────────────────────────────────────────────────────────
// Crash log: GET /api/crash.json — получить данные о последнем крэше
//           DELETE /api/crash.json — очистить
// ─────────────────────────────────────────────────────────────────────────────

static esp_err_t crash_json_get_handler(httpd_req_t* req) {
  char buf[384];
  CrashLoggerGetJson(buf, sizeof(buf));
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static esp_err_t crash_json_delete_handler(httpd_req_t* req) {
  CrashLoggerClear();
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

// ─────────────────────────────────────────────────────────────────────────────
// Binary telemetry log download: GET /api/log.bin
//
// Format (all values little-endian):
//   Section 1 — кадры телеметрии:
//     [4] uint32_t frame_count
//     [4] uint32_t frame_size   (sizeof(TelemetryLogFrame))
//     [frame_count × frame_size] raw TelemetryLogFrame[]
//
//   Section 2 — события (старт/стоп режимов и калибровок):
//     [4] uint32_t event_count
//     [4] uint32_t event_size   (sizeof(TelemetryEvent))
//     [event_count × event_size] raw TelemetryEvent[]
//   Section 3 — snapshots StabilizationConfig:
//     [4] uint32_t snapshot_count
//     [4] uint32_t snapshot_size
//     [snapshot_count × snapshot_size] raw TelemetryConfigSnapshot[]
// ─────────────────────────────────────────────────────────────────────────────

static esp_err_t log_bin_handler(httpd_req_t* req) {
  struct ConfigSnapshotExportGuard {
    bool active{false};
    ~ConfigSnapshotExportGuard() {
      if (active) VehicleControlEndConfigSnapshotExport();
    }
  } snapshot_export;

  size_t frame_count = 0;
  TelemetryLogFrame tail_frame{};
  size_t snapshot_count = 0;
  if (!VehicleControlBeginLogAndConfigExport(&frame_count, &tail_frame,
                                             &snapshot_count)) {
    return ESP_ERR_INVALID_STATE;
  }
  snapshot_export.active = true;
  const size_t event_count = VehicleControlGetEventCount();

  // The first frozen frames have no overwrite slack in a full ring. Copy them
  // before snapshot scanning or any network operation.
  constexpr size_t kFrameBatch = 32;
  TelemetryLogFrame frame_batch[kFrameBatch];
  const size_t initial_frame_count = std::min(frame_count, kFrameBatch);
  if (initial_frame_count > 0 &&
      VehicleControlCopyLogExportFrames(0, frame_batch, initial_frame_count) !=
          initial_frame_count) {
    VehicleControlEndLogExport();
    return ESP_ERR_INVALID_STATE;
  }
  if (!VehicleControlFinalizeConfigSnapshotExport(&snapshot_count)) {
    VehicleControlEndLogExport();
    return ESP_ERR_INVALID_STATE;
  }

  httpd_resp_set_type(req, "application/octet-stream");
  httpd_resp_set_hdr(req, "Content-Disposition",
                     "attachment; filename=\"telemetry_log.bin\"");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

  // ── Section 1 header: frame_count + frame_size ───────────────────────────
  const uint32_t frame_header[2] = {
      static_cast<uint32_t>(frame_count),
      static_cast<uint32_t>(sizeof(TelemetryLogFrame)),
  };
  esp_err_t err = httpd_resp_send_chunk(
      req, reinterpret_cast<const char*>(frame_header), sizeof(frame_header));
  if (err != ESP_OK) {
    VehicleControlEndLogExport();
    return err;
  }

  // ── Section 1 data: frames in batches ────────────────────────────────────
  if (initial_frame_count > 0) {
    err =
        httpd_resp_send_chunk(req, reinterpret_cast<const char*>(frame_batch),
                              initial_frame_count * sizeof(TelemetryLogFrame));
    if (err != ESP_OK) {
      VehicleControlEndLogExport();
      return err;
    }
  }
  for (size_t sent = initial_frame_count; sent < frame_count;
       sent += kFrameBatch) {
    const size_t n =
        VehicleControlCopyLogExportFrames(sent, frame_batch, kFrameBatch);
    if (n == 0) {
      VehicleControlEndLogExport();
      return ESP_ERR_INVALID_STATE;
    }
    err = httpd_resp_send_chunk(req, reinterpret_cast<const char*>(frame_batch),
                                n * sizeof(TelemetryLogFrame));
    if (err != ESP_OK) {
      VehicleControlEndLogExport();
      return err;
    }
  }
  VehicleControlEndLogExport();

  // ── Section 2 header: event_count + event_size ───────────────────────────
  const uint32_t event_header[2] = {
      static_cast<uint32_t>(event_count),
      static_cast<uint32_t>(sizeof(rc_vehicle::TelemetryEvent)),
  };
  err = httpd_resp_send_chunk(req, reinterpret_cast<const char*>(event_header),
                              sizeof(event_header));
  if (err != ESP_OK) return err;

  // ── Section 2 data: events in batches ────────────────────────────────────
  constexpr size_t kEventBatch = 64;
  rc_vehicle::TelemetryEvent event_batch[kEventBatch];

  for (size_t sent = 0; sent < event_count;) {
    size_t n = std::min(kEventBatch, event_count - sent);
    size_t filled = 0;
    for (size_t i = 0; i < n; ++i) {
      if (VehicleControlGetEvent(sent + i, &event_batch[filled])) {
        ++filled;
      }
    }
    if (filled > 0) {
      err =
          httpd_resp_send_chunk(req, reinterpret_cast<const char*>(event_batch),
                                filled * sizeof(rc_vehicle::TelemetryEvent));
      if (err != ESP_OK) return err;
    }
    sent += n;
  }

  const uint32_t snapshot_header[2] = {
      static_cast<uint32_t>(snapshot_count),
      static_cast<uint32_t>(sizeof(rc_vehicle::TelemetryConfigSnapshot)),
  };
  err =
      httpd_resp_send_chunk(req, reinterpret_cast<const char*>(snapshot_header),
                            sizeof(snapshot_header));
  if (err != ESP_OK) return err;

  for (size_t i = 0; i < snapshot_count; ++i) {
    rc_vehicle::TelemetryConfigSnapshot snapshot{};
    if (!VehicleControlGetNextConfigSnapshotExport(&snapshot)) {
      return ESP_ERR_INVALID_STATE;
    }
    err = httpd_resp_send_chunk(req, reinterpret_cast<const char*>(&snapshot),
                                sizeof(snapshot));
    if (err != ESP_OK) return err;
  }

  // End chunked response
  httpd_resp_send_chunk(req, nullptr, 0);
  ESP_LOGI(TAG, "Binary log download: %zu frames + %zu events + %zu snapshots",
           frame_count, event_count, snapshot_count);
  return ESP_OK;
}

// Логирует и пробрасывает ошибку регистрации, а не молча теряет её (main.cpp
// уже проверяет возврат RcHttpRegisterRoutes и должен получать реальный код).
static esp_err_t RegisterUri(httpd_handle_t server, const httpd_uri_t& uri) {
  esp_err_t e = httpd_register_uri_handler(server, &uri);
  if (e != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register URI %s: %s", uri.uri, esp_err_to_name(e));
  }
  return e;
}

esp_err_t RcHttpRegisterRoutes(httpd_handle_t server) {
  httpd_uri_t root_uri = {
      .uri = "/",
      .method = HTTP_GET,
      .handler = root_get_handler,
      .user_ctx = NULL,
#if CONFIG_HTTPD_WS_SUPPORT
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL,
#endif
  };
  {
    esp_err_t e = RegisterUri(server, root_uri);
    if (e != ESP_OK) return e;
  }

  httpd_uri_t css_uri = {
      .uri = "/style.css",
      .method = HTTP_GET,
      .handler = style_css_handler,
      .user_ctx = NULL,
#if CONFIG_HTTPD_WS_SUPPORT
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL,
#endif
  };
  {
    esp_err_t e = RegisterUri(server, css_uri);
    if (e != ESP_OK) return e;
  }

  httpd_uri_t js_uri = {
      .uri = "/app.js",
      .method = HTTP_GET,
      .handler = app_js_handler,
      .user_ctx = NULL,
#if CONFIG_HTTPD_WS_SUPPORT
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL,
#endif
  };
  {
    esp_err_t e = RegisterUri(server, js_uri);
    if (e != ESP_OK) return e;
  }

  httpd_uri_t log_bin_uri = {
      .uri = "/api/log.bin",
      .method = HTTP_GET,
      .handler = log_bin_handler,
      .user_ctx = NULL,
#if CONFIG_HTTPD_WS_SUPPORT
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL,
#endif
  };
  {
    esp_err_t e = RegisterUri(server, log_bin_uri);
    if (e != ESP_OK) return e;
  }

  httpd_uri_t crash_json_get_uri = {
      .uri = "/api/crash.json",
      .method = HTTP_GET,
      .handler = crash_json_get_handler,
      .user_ctx = NULL,
#if CONFIG_HTTPD_WS_SUPPORT
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL,
#endif
  };
  {
    esp_err_t e = RegisterUri(server, crash_json_get_uri);
    if (e != ESP_OK) return e;
  }

  httpd_uri_t crash_json_delete_uri = {
      .uri = "/api/crash.json",
      .method = HTTP_DELETE,
      .handler = crash_json_delete_handler,
      .user_ctx = NULL,
#if CONFIG_HTTPD_WS_SUPPORT
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL,
#endif
  };
  {
    esp_err_t e = RegisterUri(server, crash_json_delete_uri);
    if (e != ESP_OK) return e;
  }

  return ESP_OK;
}
