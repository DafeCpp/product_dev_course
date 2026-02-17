#include "http_server.hpp"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "config.hpp"
#include "esp_http_server.h"
#include "esp_log.h"
#include "wifi_ap.hpp"

static const char* TAG = "http_server";
static httpd_handle_t server_handle = NULL;

httpd_handle_t HttpServerGetHandle(void) { return server_handle; }

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

static esp_err_t SendWifiStatusJson(httpd_req_t* req) {
  char ap_ip[16] = {};
  char ap_ssid[32] = {};
  (void)WiFiApGetIp(ap_ip, sizeof(ap_ip));
  (void)WiFiApGetSsid(ap_ssid, sizeof(ap_ssid));

  WiFiStaStatus sta = {};
  (void)WiFiStaGetStatus(&sta);

  cJSON* root = cJSON_CreateObject();
  if (!root) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Failed to allocate JSON");
    return ESP_FAIL;
  }

  cJSON* ap = cJSON_CreateObject();
  if (ap) {
    cJSON_AddStringToObject(ap, "ssid", ap_ssid);
    cJSON_AddStringToObject(ap, "ip", ap_ip);
    cJSON_AddItemToObject(root, "ap", ap);
  }

  cJSON* sta_obj = cJSON_CreateObject();
  if (sta_obj) {
    cJSON_AddBoolToObject(sta_obj, "configured", sta.configured);
    cJSON_AddBoolToObject(sta_obj, "connected", sta.connected);
    cJSON_AddNumberToObject(sta_obj, "reason", sta.last_disconnect_reason);
    cJSON_AddNumberToObject(sta_obj, "rssi", sta.rssi);
    cJSON_AddStringToObject(sta_obj, "ssid", sta.ssid);
    cJSON_AddStringToObject(sta_obj, "ip", sta.ip);
    cJSON_AddItemToObject(root, "sta", sta_obj);
  }

  char* json_str = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!json_str) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Failed to render JSON");
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
  free(json_str);
  return ESP_OK;
}

static esp_err_t wifi_status_handler(httpd_req_t* req) {
  if (req->method != HTTP_GET) {
    httpd_resp_send_err(req, HTTPD_405_METHOD_NOT_ALLOWED, "GET only");
    return ESP_FAIL;
  }
  return SendWifiStatusJson(req);
}

static esp_err_t ReadJsonBody(httpd_req_t* req, char* buf, size_t buf_len) {
  if (!req || !buf || buf_len == 0) return ESP_ERR_INVALID_ARG;
  buf[0] = '\0';

  size_t total_len = req->content_len;
  if (total_len == 0) return ESP_OK;
  if (total_len >= buf_len) return ESP_ERR_INVALID_SIZE;

  size_t cur = 0;
  while (cur < total_len) {
    int ret = httpd_req_recv(req, buf + cur, total_len - cur);
    if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
      continue;
    }
    if (ret <= 0) {
      return ESP_FAIL;
    }
    cur += (size_t)ret;
  }
  buf[cur] = '\0';
  return ESP_OK;
}

static esp_err_t wifi_sta_connect_handler(httpd_req_t* req) {
  char body[256];
  esp_err_t e = ReadJsonBody(req, body, sizeof(body));
  if (e != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad request");
    return ESP_FAIL;
  }

  cJSON* json = cJSON_Parse(body);
  if (!json) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    return ESP_FAIL;
  }

  const cJSON* ssid = cJSON_GetObjectItem(json, "ssid");
  const cJSON* password = cJSON_GetObjectItem(json, "password");
  const cJSON* save = cJSON_GetObjectItem(json, "save");

  const char* ssid_str =
      (ssid && cJSON_IsString(ssid)) ? ssid->valuestring : "";
  const char* pass_str =
      (password && cJSON_IsString(password)) ? password->valuestring : "";
  bool save_cfg = true;
  if (save && cJSON_IsBool(save)) save_cfg = cJSON_IsTrue(save);

  (void)WiFiStaConnect(ssid_str, pass_str, save_cfg);
  cJSON_Delete(json);

  return SendWifiStatusJson(req);
}

static esp_err_t wifi_sta_disconnect_handler(httpd_req_t* req) {
  char body[128];
  bool forget = false;
  if (ReadJsonBody(req, body, sizeof(body)) == ESP_OK && body[0] != '\0') {
    cJSON* json = cJSON_Parse(body);
    if (json) {
      const cJSON* f = cJSON_GetObjectItem(json, "forget");
      if (f && cJSON_IsBool(f)) forget = cJSON_IsTrue(f);
      cJSON_Delete(json);
    }
  }

  (void)WiFiStaDisconnect(forget);
  return SendWifiStatusJson(req);
}

static esp_err_t wifi_scan_handler(httpd_req_t* req) {
  if (req->method != HTTP_GET) {
    httpd_resp_send_err(req, HTTPD_405_METHOD_NOT_ALLOWED, "GET only");
    return ESP_FAIL;
  }

  WiFiScanNetwork nets[20] = {};
  size_t count = sizeof(nets) / sizeof(nets[0]);
  esp_err_t e = WiFiStaScan(nets, &count);
  if (e != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Scan failed");
    return ESP_FAIL;
  }

  cJSON* root = cJSON_CreateObject();
  if (!root) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Failed to allocate JSON");
    return ESP_FAIL;
  }

  cJSON* arr = cJSON_CreateArray();
  if (!arr) {
    cJSON_Delete(root);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Failed to allocate JSON");
    return ESP_FAIL;
  }
  cJSON_AddItemToObject(root, "networks", arr);

  for (size_t i = 0; i < count; i++) {
    if (nets[i].ssid[0] == '\0') continue;
    cJSON* n = cJSON_CreateObject();
    if (!n) continue;
    cJSON_AddStringToObject(n, "ssid", nets[i].ssid);
    cJSON_AddNumberToObject(n, "rssi", nets[i].rssi);
    cJSON_AddNumberToObject(n, "channel", nets[i].channel);
    cJSON_AddNumberToObject(n, "authmode", nets[i].authmode);
    cJSON_AddBoolToObject(n, "open", nets[i].authmode == 0);
    cJSON_AddItemToArray(arr, n);
  }

  char* json_str = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!json_str) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Failed to render JSON");
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
  free(json_str);
  return ESP_OK;
}

static esp_err_t root_get_handler(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, reinterpret_cast<const char*>(INDEX_HTML),
                  INDEX_HTML_LEN);
  return ESP_OK;
}

static esp_err_t style_css_handler(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/css");
  httpd_resp_send(req, reinterpret_cast<const char*>(STYLE_CSS), STYLE_CSS_LEN);
  return ESP_OK;
}

static esp_err_t app_js_handler(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/javascript");
  httpd_resp_send(req, reinterpret_cast<const char*>(APP_JS), APP_JS_LEN);
  return ESP_OK;
}

esp_err_t HttpServerInit(void) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = HTTP_SERVER_PORT;
  config.max_uri_handlers = 16;
  config.stack_size = 8192;
  config.max_open_sockets = 7;
  config.lru_purge_enable = true;  // Автозакрытие старых соединений при нехватке

  ESP_LOGI(TAG, "Starting HTTP server on port %d", config.server_port);

  if (httpd_start(&server_handle, &config) == ESP_OK) {
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
    httpd_register_uri_handler(server_handle, &root_uri);

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
    httpd_register_uri_handler(server_handle, &css_uri);

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
    httpd_register_uri_handler(server_handle, &js_uri);

    httpd_uri_t wifi_status_uri = {
        .uri = "/api/wifi/status",
        .method = HTTP_GET,
        .handler = wifi_status_handler,
        .user_ctx = NULL,
#if CONFIG_HTTPD_WS_SUPPORT
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL,
#endif
    };
    httpd_register_uri_handler(server_handle, &wifi_status_uri);

    httpd_uri_t wifi_connect_uri = {
        .uri = "/api/wifi/sta/connect",
        .method = HTTP_POST,
        .handler = wifi_sta_connect_handler,
        .user_ctx = NULL,
#if CONFIG_HTTPD_WS_SUPPORT
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL,
#endif
    };
    httpd_register_uri_handler(server_handle, &wifi_connect_uri);

    httpd_uri_t wifi_disconnect_uri = {
        .uri = "/api/wifi/sta/disconnect",
        .method = HTTP_POST,
        .handler = wifi_sta_disconnect_handler,
        .user_ctx = NULL,
#if CONFIG_HTTPD_WS_SUPPORT
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL,
#endif
    };
    httpd_register_uri_handler(server_handle, &wifi_disconnect_uri);

    httpd_uri_t wifi_scan_uri = {
        .uri = "/api/wifi/scan",
        .method = HTTP_GET,
        .handler = wifi_scan_handler,
        .user_ctx = NULL,
#if CONFIG_HTTPD_WS_SUPPORT
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL,
#endif
    };
    httpd_register_uri_handler(server_handle, &wifi_scan_uri);

    ESP_LOGI(TAG, "HTTP server started");
    return ESP_OK;
  }

  ESP_LOGE(TAG, "Failed to start HTTP server");
  return ESP_FAIL;
}
