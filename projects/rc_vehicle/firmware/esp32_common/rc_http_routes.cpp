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

#if 0
// Legacy inline web/app.js (kept for reference).
static const char APP_JS[] = R"web(
// WebSocket подключение
let ws = null;
let wsReconnectInterval = null;
const WS_URL = `ws://${window.location.hostname}:81/ws`;

// Элементы UI
const wsStatusEl = document.getElementById('ws-status');
const mcuStatusEl = document.getElementById('mcu-status');
const throttleSlider = document.getElementById('throttle');
const steeringSlider = document.getElementById('steering');
const throttleValueEl = document.getElementById('throttle-value');
const steeringValueEl = document.getElementById('steering-value');
const btnCenter = document.getElementById('btn-center');
const btnStop = document.getElementById('btn-stop');
const telemDataEl = document.getElementById('telem-data');

// Wi‑Fi STA UI
const staStatusEl = document.getElementById('sta-status');
const staSsidEl = document.getElementById('sta-ssid');
const staIpEl = document.getElementById('sta-ip');
const staScanList = document.getElementById('sta-scan-list');
const btnStaScan = document.getElementById('btn-sta-scan');
const staSsidInput = document.getElementById('sta-ssid-input');
const staPassInput = document.getElementById('sta-pass-input');
const btnStaConnect = document.getElementById('btn-sta-connect');
const btnStaDisconnect = document.getElementById('btn-sta-disconnect');
const btnStaForget = document.getElementById('btn-sta-forget');

// Состояние
let lastCommandSeq = 0;
let commandSendInterval = null;
let lastTelemTime = 0;
const MCU_TIMEOUT_MS = 1500;
let mcuStatusCheckInterval = null;
let wifiStatusInterval = null;

// Подключение к WebSocket
function connectWebSocket() {
    try {
        ws = new WebSocket(WS_URL);

        ws.onopen = () => {
            console.log('WebSocket connected');
            wsStatusEl.textContent = 'Подключено';
            wsStatusEl.className = 'status-value connected';
            clearInterval(wsReconnectInterval);
            lastTelemTime = 0;
            setMcuStatus('unknown');
            startMcuStatusCheck();
            startCommandSending();
        };

        ws.onmessage = (event) => {
            try {
                const data = JSON.parse(event.data);
                if (data.type === 'telem') {
                    updateTelem(data);
                }
            } catch (e) {
                console.error('Failed to parse telem:', e);
            }
        };

        ws.onerror = (error) => {
            console.error('WebSocket error:', error);
        };

        ws.onclose = () => {
            console.log('WebSocket disconnected');
            wsStatusEl.textContent = 'Отключено';
            wsStatusEl.className = 'status-value disconnected';
            stopCommandSending();
            stopMcuStatusCheck();
            setMcuStatus('unknown');

            // Переподключение через 2 секунды
            wsReconnectInterval = setInterval(connectWebSocket, 2000);
        };
    } catch (e) {
        console.error('Failed to connect WebSocket:', e);
    }
}

// Отправка команды управления
function sendCommand() {
    if (!ws || ws.readyState !== WebSocket.OPEN) {
        return;
    }

    const throttle = parseFloat(throttleSlider.value);
    const steering = parseFloat(steeringSlider.value);

    const command = {
        type: 'cmd',
        throttle: throttle,
        steering: steering,
        seq: ++lastCommandSeq
    };

    ws.send(JSON.stringify(command));
}

// Запуск периодической отправки команд (50 Hz = каждые 20 мс)
function startCommandSending() {
    if (commandSendInterval) {
        clearInterval(commandSendInterval);
    }
    commandSendInterval = setInterval(sendCommand, 20);
}

// Остановка отправки команд
function stopCommandSending() {
    if (commandSendInterval) {
        clearInterval(commandSendInterval);
        commandSendInterval = null;
    }
}

// Статус подключения Pico/STM (по факту прихода телеметрии по UART)
function setMcuStatus(state) {
    if (!mcuStatusEl) return;
    if (state === 'connected') {
        mcuStatusEl.textContent = 'Подключено';
        mcuStatusEl.className = 'status-value connected';
    } else if (state === 'disconnected') {
        mcuStatusEl.textContent = 'Нет связи';
        mcuStatusEl.className = 'status-value disconnected';
    } else {
        mcuStatusEl.textContent = '—';
        mcuStatusEl.className = 'status-value unknown';
    }
}

function startMcuStatusCheck() {
    if (mcuStatusCheckInterval) clearInterval(mcuStatusCheckInterval);
    mcuStatusCheckInterval = setInterval(() => {
        if (lastTelemTime && (Date.now() - lastTelemTime > MCU_TIMEOUT_MS)) {
            setMcuStatus('disconnected');
        }
    }, 500);
}

function stopMcuStatusCheck() {
    if (mcuStatusCheckInterval) {
        clearInterval(mcuStatusCheckInterval);
        mcuStatusCheckInterval = null;
    }
}

function setStaStatus(state) {
    if (!staStatusEl) return;
    if (state === 'connected') {
        staStatusEl.textContent = 'Подключено';
        staStatusEl.className = 'status-value connected';
    } else if (state === 'disconnected') {
        staStatusEl.textContent = 'Нет связи';
        staStatusEl.className = 'status-value disconnected';
    } else if (state === 'configured') {
        staStatusEl.textContent = 'Настроено';
        staStatusEl.className = 'status-value unknown';
    } else {
        staStatusEl.textContent = '—';
        staStatusEl.className = 'status-value unknown';
    }
}

function updateSta(sta) {
    if (!sta) {
        setStaStatus('unknown');
        if (staSsidEl) staSsidEl.textContent = '—';
        if (staIpEl) staIpEl.textContent = '—';
        return;
    }

    const ssid = sta.ssid || '';
    const ip = sta.ip || '';
    const configured = !!sta.configured;
    const connected = !!sta.connected;

    if (connected) {
        setStaStatus('connected');
    } else if (configured) {
        setStaStatus('disconnected');
    } else {
        setStaStatus('unknown');
    }

    if (staSsidEl) {
        staSsidEl.textContent = ssid || '—';
        staSsidEl.className = 'status-value ' + (configured ? 'connected' : 'unknown');
    }

    if (staIpEl) {
        staIpEl.textContent = ip || '—';
        staIpEl.className = 'status-value ' + (connected ? 'connected' : 'unknown');
    }

    // Для удобства: подставляем SSID в инпут (пароль не отображаем)
    if (staSsidInput && ssid && !staSsidInput.value) {
        staSsidInput.value = ssid;
    }
}

async function fetchWifiStatus() {
    try {
        const resp = await fetch('/api/wifi/status', { cache: 'no-store' });
        if (!resp.ok) return;
        const data = await resp.json();
        updateSta(data.sta);
    } catch (e) {
        // Молча: если сеть/канал прыгает при AP+STA, возможны краткие ошибки.
    }
}

function renderWifiScanResults(networks) {
    if (!staScanList) return;

    const list = Array.isArray(networks) ? networks : [];
    staScanList.innerHTML = '';

    const placeholder = document.createElement('option');
    placeholder.value = '';
    placeholder.textContent = list.length ? 'Выберите сеть…' : 'Сети не найдены';
    staScanList.appendChild(placeholder);

    for (const n of list) {
        const ssid = (n?.ssid || '').trim();
        if (!ssid) continue;
        const rssi = (typeof n.rssi === 'number') ? n.rssi : null;
        const ch = (typeof n.channel === 'number') ? n.channel : null;
        const open = !!n.open;
        const sec = open ? 'open' : 'secured';

        const opt = document.createElement('option');
        opt.value = ssid;
        opt.dataset.open = open ? '1' : '0';
        opt.textContent = `${ssid}` +
            (rssi !== null ? ` (${rssi} dBm)` : '') +
            (ch !== null ? ` ch${ch}` : '') +
            ` ${sec}`;
        staScanList.appendChild(opt);
    }
}

async function scanWifiNetworks() {
    if (!btnStaScan) return;
    const prevText = btnStaScan.textContent;
    btnStaScan.disabled = true;
    btnStaScan.textContent = 'Сканирование...';

    try {
        const resp = await fetch('/api/wifi/scan', { cache: 'no-store' });
        if (resp.ok) {
            const data = await resp.json();
            const networks = data.networks || [];
            networks.sort((a, b) => (b.rssi || -100) - (a.rssi || -100));
            renderWifiScanResults(networks);
        }
    } catch (e) {
        // ignore
    }

    btnStaScan.disabled = false;
    btnStaScan.textContent = prevText;
}

// Обновление телеметрии (статус Pico/STM: по mcu_pong_ok с ESP32 или по факту прихода телеметрии)
function updateTelem(data) {
    lastTelemTime = Date.now();
    if (data.mcu_pong_ok !== undefined) {
        setMcuStatus(data.mcu_pong_ok ? 'connected' : 'disconnected');
    } else {
        setMcuStatus('connected');
    }

    let html = '';
    if (data.imu) {
        html += `<div class="telem-item">
            <span class="telem-label">Accel X:</span>
            <span class="telem-value">${data.imu.ax?.toFixed(2) || 'N/A'}</span>
        </div>`;
        html += `<div class="telem-item">
            <span class="telem-label">Accel Y:</span>
            <span class="telem-value">${data.imu.ay?.toFixed(2) || 'N/A'}</span>
        </div>`;
        html += `<div class="telem-item">
            <span class="telem-label">Accel Z:</span>
            <span class="telem-value">${data.imu.az?.toFixed(2) || 'N/A'}</span>
        </div>`;
        html += `<div class="telem-item">
            <span class="telem-label">Gyro X:</span>
            <span class="telem-value">${data.imu.gx?.toFixed(2) || 'N/A'}</span>
        </div>`;
        html += `<div class="telem-item">
            <span class="telem-label">Gyro Y:</span>
            <span class="telem-value">${data.imu.gy?.toFixed(2) || 'N/A'}</span>
        </div>`;
        html += `<div class="telem-item">
            <span class="telem-label">Gyro Z:</span>
            <span class="telem-value">${data.imu.gz?.toFixed(2) || 'N/A'}</span>
        </div>`;
    }
    if (data.act) {
        html += `<div class="telem-item">
            <span class="telem-label">Throttle:</span>
            <span class="telem-value">${data.act.throttle?.toFixed(2) || 'N/A'}</span>
        </div>`;
        html += `<div class="telem-item">
            <span class="telem-label">Steering:</span>
            <span class="telem-value">${data.act.steering?.toFixed(2) || 'N/A'}</span>
        </div>`;
    }

    telemDataEl.innerHTML = html || '<p>Нет данных</p>';
}

// Обработчики событий
throttleSlider.addEventListener('input', (e) => {
    throttleValueEl.textContent = parseFloat(e.target.value).toFixed(2);
});

steeringSlider.addEventListener('input', (e) => {
    steeringValueEl.textContent = parseFloat(e.target.value).toFixed(2);
});

btnCenter.addEventListener('click', () => {
    throttleSlider.value = 0;
    steeringSlider.value = 0;
    throttleValueEl.textContent = '0.00';
    steeringValueEl.textContent = '0.00';
});

btnStop.addEventListener('click', () => {
    throttleSlider.value = 0;
    throttleValueEl.textContent = '0.00';
});

if (staScanList) {
    staScanList.addEventListener('change', async () => {
        const ssid = (staScanList.value || '').trim();
        if (!ssid) return;

        if (staSsidInput) {
            staSsidInput.value = ssid;
        }

        const opt = staScanList.selectedOptions && staScanList.selectedOptions[0];
        const isOpen = !!opt && opt.dataset && opt.dataset.open === '1';

        if (isOpen) {
            // Открытая сеть: подключаемся сразу (без пароля)
            if (staPassInput) staPassInput.value = '';
            try {
                await fetch('/api/wifi/sta/connect', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ ssid, password: '', save: true })
                });
            } catch (e) {
                // ignore
            }
            setTimeout(fetchWifiStatus, 300);
            return;
        }

        // Защищённая сеть: спрашиваем пароль и подключаемся
        const defaultPass = staPassInput ? staPassInput.value : '';
        const pass = prompt(`Пароль для сети "${ssid}"`, defaultPass);
        if (pass === null) return; // cancel
        if (staPassInput) staPassInput.value = pass;

        try {
            await fetch('/api/wifi/sta/connect', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ ssid, password: pass, save: true })
            });
        } catch (e) {
            // ignore
        }
        setTimeout(fetchWifiStatus, 300);
    });
}

if (btnStaScan) {
    btnStaScan.addEventListener('click', async () => {
        await scanWifiNetworks();
    });
}

btnStaConnect.addEventListener('click', async () => {
    const ssid = (staSsidInput?.value || '').trim();
    const password = staPassInput?.value || '';
    if (!ssid) return;

    try {
        await fetch('/api/wifi/sta/connect', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ ssid, password, save: true })
        });
    } catch (e) {
        // ignore
    }
    setTimeout(fetchWifiStatus, 300);
});

btnStaDisconnect.addEventListener('click', async () => {
    try {
        await fetch('/api/wifi/sta/disconnect', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ forget: false })
        });
    } catch (e) {
        // ignore
    }
    setTimeout(fetchWifiStatus, 300);
});

btnStaForget.addEventListener('click', async () => {
    try {
        await fetch('/api/wifi/sta/disconnect', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ forget: true })
        });
    } catch (e) {
        // ignore
    }
    if (staSsidInput) staSsidInput.value = '';
    if (staPassInput) staPassInput.value = '';
    setTimeout(fetchWifiStatus, 300);
});

// Инициализация при загрузке страницы
window.addEventListener('load', () => {
    connectWebSocket();
    fetchWifiStatus();
    if (wifiStatusInterval) clearInterval(wifiStatusInterval);
    wifiStatusInterval = setInterval(fetchWifiStatus, 1000);
});

// Отключение при закрытии страницы
window.addEventListener('beforeunload', () => {
    stopCommandSending();
    if (wifiStatusInterval) {
        clearInterval(wifiStatusInterval);
        wifiStatusInterval = null;
    }
    if (ws) {
        ws.close();
    }
});
)web";
#endif

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
