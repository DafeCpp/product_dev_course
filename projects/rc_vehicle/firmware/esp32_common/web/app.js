// ═══════════════════════════════════════════════════════════════════
// RC Vehicle — Web UI
// ═══════════════════════════════════════════════════════════════════

// ── WebSocket ──
let ws = null;
let wsReconnectTimer = null;
const WS_URL = `ws://${window.location.hostname}/ws`;

// ── Wake Lock (предотвращает засыпание экрана) ──
let wakeLock = null;

async function requestWakeLock() {
    if (!('wakeLock' in navigator)) return;
    try {
        wakeLock = await navigator.wakeLock.request('screen');
        wakeLock.addEventListener('release', () => { wakeLock = null; });
    } catch (e) {
        // Браузер отклонил (например, low battery) — не критично
    }
}

function releaseWakeLock() {
    if (wakeLock) { wakeLock.release(); wakeLock = null; }
}

function wsSend(obj) {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify(obj));
    }
}

// ── DOM refs ──
const $ = (id) => document.getElementById(id);
const wsStatusEl       = $('ws-status');
const mcuStatusEl      = $('mcu-status');
const throttleSlider   = $('throttle');
const steeringSlider   = $('steering');
const throttleValueEl  = $('throttle-value');
const steeringValueEl  = $('steering-value');
const btnCenter        = $('btn-center');
const btnStop          = $('btn-stop');
const telemDataEl      = $('telem-data');
const telemCounterEl   = $('telem-counter');
const uptimeBadgeEl    = $('uptime-badge');
const calibStatusEl    = $('calib-status');
const calibValidEl     = $('calib-valid');
const calibBiasEl      = $('calib-bias');
const btnCalibGyro     = $('btn-calib-gyro');
const btnCalibFull     = $('btn-calib-full');
const btnCalibForward  = $('btn-calib-forward');
const btnCalibAutoFwd  = $('btn-calib-auto-forward');
const btnCalibTrim     = $('btn-calib-trim');
const trimCalibStatusEl = $('trim-calib-status');
const calibFwdAccelSlider    = $('calib-fwd-accel');
const calibFwdAccelValueEl   = $('calib-fwd-accel-value');
// Backward compat
const calibFwdThrottleSlider = $('calib-fwd-throttle');
const calibFwdThrottleValueEl= $('calib-fwd-throttle-value');

// Wi-Fi STA
const staStatusEl      = $('sta-status');
const staSsidEl        = $('sta-ssid');
const staIpEl          = $('sta-ip');
const staScanList      = $('sta-scan-list');
const btnStaScan       = $('btn-sta-scan');
const staSsidInput     = $('sta-ssid-input');
const staPassInput     = $('sta-pass-input');
const btnStaConnect    = $('btn-sta-connect');
const btnStaDisconnect = $('btn-sta-disconnect');
const btnStaForget     = $('btn-sta-forget');

// ── State ──
let lastCommandSeq = 0;
let commandSendInterval = null;
let lastTelemTime = 0;
let wsConnectTime = 0;
let telemRxCount = 0;
const MCU_TIMEOUT_MS = 1500;
let mcuStatusCheckInterval = null;
let wifiStatusInterval = null;

// ── Accordion ──
document.querySelectorAll('.panel-header').forEach(hdr => {
    hdr.addEventListener('click', () => {
        const panel = hdr.closest('.panel');
        const body = panel.querySelector('.panel-body');
        const open = panel.classList.toggle('open');
        hdr.setAttribute('aria-expanded', open);
        if (open) {
            body.removeAttribute('hidden');
        } else {
            body.setAttribute('hidden', '');
        }
    });
});

// ═══════════════════════════════════════════════════════════════════
// WebSocket connection
// ═══════════════════════════════════════════════════════════════════

function scheduleReconnect(delayMs) {
    if (wsReconnectTimer) clearTimeout(wsReconnectTimer);
    wsReconnectTimer = setTimeout(connectWebSocket, delayMs);
}

function connectWebSocket() {
    // Очистить pending таймер
    if (wsReconnectTimer) { clearTimeout(wsReconnectTimer); wsReconnectTimer = null; }

    // Закрыть zombie-соединение
    if (ws) {
        try { ws.onclose = null; ws.onerror = null; ws.close(); } catch (_) {}
        ws = null;
    }

    try {
        ws = new WebSocket(WS_URL);

        ws.onopen = () => {
            wsStatusEl.textContent = 'WS';
            wsStatusEl.className = 'badge badge-on';
            lastTelemTime = 0;
            wsConnectTime = Date.now();
            setMcuStatus('unknown');
            startMcuStatusCheck();
            startCommandSending();
            requestWakeLock();
        };

        ws.onmessage = (event) => {
            try {
                const data = JSON.parse(event.data);
                if (data.type === 'telem') {
                    updateTelem(data);
                } else if (data.type === 'calibrate_imu_ack') {
                    updateCalibStatus(data.status, null);
                } else if (data.type === 'calib_status') {
                    updateCalibStatus(data.status, null);
                } else if (data.type === 'calibrate_steering_trim_ack') {
                    updateTrimCalibStatus(data);
                } else if (data.type === 'steering_trim_status') {
                    updateTrimCalibStatus(data);
                } else if (data.type === 'calibrate_com_offset_ack') {
                    updateComCalibStatus(data);
                } else if (data.type === 'com_offset_status') {
                    updateComCalibStatus(data);
                } else if (data.type === 'start_test_ack') {
                    updateTestStatus(data);
                } else if (data.type === 'test_status') {
                    updateTestStatus(data);
                } else if (data.type === 'stop_test_ack') {
                    updateTestStatus(data);
                } else if (data.type === 'stab_config' || data.type === 'set_stab_config_ack') {
                    if (data.type === 'stab_config') {
                        applyStabConfig(data);
                    } else if (data.ok) {
                        applyStabConfig(data);
                        showStabSaveStatus('Сохранено', 'toast-ok');
                    } else {
                        showStabSaveStatus('Ошибка сохранения', 'toast-err');
                    }
                } else if (data.type === 'log_info') {
                    updateLogInfo(data.count, data.capacity);
                    if (pendingLogTotal === -2) {
                        const total = data.count || 0;
                        const want = total;
                        const offset = 0;
                        pendingLogTotal = want;
                        pendingLogOffset = offset;
                        pendingLogFrames = [];
                        if (want > 0) {
                            wsSend({ type: 'get_log_data', offset: offset, count: Math.min(200, want) });
                        } else {
                            exportLogCsv([]);
                        }
                    }
                } else if (data.type === 'log_data') {
                    handleLogData(data.frames || []);
                } else if (data.type === 'clear_log_ack') {
                    wsSend({ type: 'get_log_info' });
                } else if (data.type === 'self_test_result') {
                    showSelfTestResult(data);
                } else if (data.type === 'toggle_kids_mode_ack') {
                    kidsMode = data.active;
                    updateKidsModeUI();
                }
            } catch (e) {
                console.error('Parse error:', e, event.data?.slice(0, 200));
                if (telemDataEl) {
                    telemDataEl.innerHTML = `<p style="color:var(--danger)">JS error: ${e.message}</p>`;
                }
            }
        };

        ws.onerror = (error) => console.error('WS error:', error);

        ws.onclose = () => {
            ws = null;
            wsStatusEl.textContent = 'WS';
            wsStatusEl.className = 'badge badge-off';
            stopCommandSending();
            stopMcuStatusCheck();
            setMcuStatus('unknown');
            scheduleReconnect(2000);
        };
    } catch (e) {
        console.error('WS connect failed:', e);
        scheduleReconnect(2000);
    }
}

// ═══════════════════════════════════════════════════════════════════
// Command sending
// ═══════════════════════════════════════════════════════════════════

function sendCommand() {
    if (!ws || ws.readyState !== WebSocket.OPEN) return;

    let throttle = parseFloat(throttleSlider.value);
    let steering = parseFloat(steeringSlider.value);

    if (kidsMode) {
        throttle = Math.max(-kidsThrottleLimit, Math.min(kidsThrottleLimit, throttle));
        steering = Math.max(-kidsSteeringLimit, Math.min(kidsSteeringLimit, steering));
    }

    ws.send(JSON.stringify({
        type: 'cmd',
        throttle, steering,
        seq: ++lastCommandSeq
    }));
}

function startCommandSending() {
    if (commandSendInterval) clearInterval(commandSendInterval);
    commandSendInterval = setInterval(sendCommand, 20);
}

function stopCommandSending() {
    if (commandSendInterval) { clearInterval(commandSendInterval); commandSendInterval = null; }
}

// ═══════════════════════════════════════════════════════════════════
// MCU status
// ═══════════════════════════════════════════════════════════════════

function setMcuStatus(state) {
    if (!mcuStatusEl) return;
    if (state === 'connected') {
        mcuStatusEl.textContent = 'MCU';
        mcuStatusEl.className = 'badge badge-on';
    } else if (state === 'disconnected') {
        mcuStatusEl.textContent = 'MCU';
        mcuStatusEl.className = 'badge badge-off';
    } else {
        mcuStatusEl.textContent = 'MCU';
        mcuStatusEl.className = 'badge badge-unknown';
    }
}

function startMcuStatusCheck() {
    if (mcuStatusCheckInterval) clearInterval(mcuStatusCheckInterval);
    mcuStatusCheckInterval = setInterval(() => {
        const baseline = lastTelemTime || wsConnectTime;
        if (Date.now() - baseline > MCU_TIMEOUT_MS) setMcuStatus('disconnected');
    }, 500);
}

function stopMcuStatusCheck() {
    if (mcuStatusCheckInterval) { clearInterval(mcuStatusCheckInterval); mcuStatusCheckInterval = null; }
}

// ═══════════════════════════════════════════════════════════════════
// Wi-Fi STA
// ═══════════════════════════════════════════════════════════════════

function setStaStatus(state) {
    if (!staStatusEl) return;
    const map = {
        connected:    ['badge-on', 'Подключено'],
        disconnected: ['badge-off', 'Нет связи'],
        configured:   ['badge-warn', 'Настроено'],
    };
    const [cls, text] = map[state] || ['badge-unknown', '—'];
    staStatusEl.textContent = text;
    staStatusEl.className = 'badge ' + cls;
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

    if (connected) setStaStatus('connected');
    else if (configured) setStaStatus('disconnected');
    else setStaStatus('unknown');

    if (staSsidEl) staSsidEl.textContent = ssid || '—';
    if (staIpEl) staIpEl.textContent = ip || '—';
    if (staSsidInput && ssid && !staSsidInput.value) staSsidInput.value = ssid;
}

async function fetchWifiStatus() {
    try {
        const resp = await fetch('/api/wifi/status', { cache: 'no-store' });
        if (resp.ok) updateSta(await resp.json().then(d => d.sta));
    } catch (e) { /* ignore */ }
}

function renderWifiScanResults(networks) {
    if (!staScanList) return;
    const list = Array.isArray(networks) ? networks : [];
    staScanList.innerHTML = '';

    const ph = document.createElement('option');
    ph.value = '';
    ph.textContent = list.length ? 'Выберите сеть...' : 'Сети не найдены';
    staScanList.appendChild(ph);

    for (const n of list) {
        const ssid = (n?.ssid || '').trim();
        if (!ssid) continue;
        const rssi = typeof n.rssi === 'number' ? n.rssi : null;
        const ch = typeof n.channel === 'number' ? n.channel : null;
        const open = !!n.open;
        const opt = document.createElement('option');
        opt.value = ssid;
        opt.dataset.open = open ? '1' : '0';
        opt.textContent = ssid
            + (rssi !== null ? ` (${rssi} dBm)` : '')
            + (ch !== null ? ` ch${ch}` : '')
            + (open ? ' open' : '');
        staScanList.appendChild(opt);
    }
}

async function scanWifiNetworks() {
    if (!btnStaScan) return;
    const prev = btnStaScan.textContent;
    btnStaScan.disabled = true;
    btnStaScan.textContent = 'Сканирование...';
    try {
        const resp = await fetch('/api/wifi/scan', { cache: 'no-store' });
        if (resp.ok) {
            const networks = (await resp.json()).networks || [];
            networks.sort((a, b) => (b.rssi || -100) - (a.rssi || -100));
            renderWifiScanResults(networks);
        }
    } catch (e) { /* ignore */ }
    btnStaScan.disabled = false;
    btnStaScan.textContent = prev;
}

// ═══════════════════════════════════════════════════════════════════
// Calibration
// ═══════════════════════════════════════════════════════════════════

function updateCalibStatus(status, calib) {
    if (calibStatusEl && status) {
        const labels = {
            idle: 'Ожидание', collecting: 'Сбор...', done: 'Готово', failed: 'Ошибка'
        };
        calibStatusEl.textContent = labels[status] || status;
        const classMap = {
            idle: 'badge-unknown', collecting: 'badge-pulse badge-warn',
            done: 'badge-on', failed: 'badge-off'
        };
        calibStatusEl.className = 'badge ' + (classMap[status] || 'badge-unknown');

        const busy = status === 'collecting';
        [btnCalibGyro, btnCalibFull, btnCalibForward, btnCalibAutoFwd].forEach(b => { if (b) b.disabled = busy; });
    }

    if (calib) {
        if (calibValidEl) {
            calibValidEl.textContent = calib.valid ? 'Валидны' : 'Нет';
            calibValidEl.className = 'badge ' + (calib.valid ? 'badge-on' : 'badge-unknown');
        }
        if (calib.bias && calibBiasEl) {
            calibBiasEl.style.display = 'block';
            const set = (id, v) => { const el = $(id); if (el) el.textContent = typeof v === 'number' ? v.toFixed(4) : '—'; };
            set('calib-gx', calib.bias.gx); set('calib-gy', calib.bias.gy); set('calib-gz', calib.bias.gz);
            set('calib-ax', calib.bias.ax); set('calib-ay', calib.bias.ay); set('calib-az', calib.bias.az);
        } else if (calibBiasEl && !calib.valid) {
            calibBiasEl.style.display = 'none';
        }
    }
}

function sendCalibrate(mode) {
    if (!ws || ws.readyState !== WebSocket.OPEN) return;
    ws.send(JSON.stringify({ type: 'calibrate_imu', mode }));
}

// ═══════════════════════════════════════════════════════════════════
// Live charts
// ═══════════════════════════════════════════════════════════════════

const CHART_WINDOW = 200; // rolling buffer size (~10s at 20Hz)

// ChartBuffer: ring buffer for N traces
function ChartBuffer(traceCount) {
    this.size = CHART_WINDOW;
    this.len = 0;
    this.head = 0;
    this.data = [];
    for (let i = 0; i < traceCount; i++) {
        this.data.push(new Float32Array(CHART_WINDOW));
    }
}
ChartBuffer.prototype.push = function(values) {
    for (let i = 0; i < this.data.length; i++) {
        this.data[i][this.head] = values[i] !== undefined ? values[i] : 0;
    }
    this.head = (this.head + 1) % this.size;
    if (this.len < this.size) this.len++;
};
ChartBuffer.prototype.getTrace = function(traceIdx) {
    const arr = this.data[traceIdx];
    const out = new Float32Array(this.len);
    const start = this.len < this.size ? 0 : this.head;
    for (let i = 0; i < this.len; i++) {
        out[i] = arr[(start + i) % this.size];
    }
    return out;
};

// Chart definitions
const chartDefs = {
    accel: {
        canvasId: 'chart-accel',
        legendId: 'legend-accel',
        title: 'Акселерометр',
        traces: [
            { label: 'ax', color: '#5b8af5' },
            { label: 'ay', color: '#34c759' },
            { label: 'az', color: '#ff9f0a' },
        ],
        buf: new ChartBuffer(3),
        lastVals: [0, 0, 0],
    },
    gyro: {
        canvasId: 'chart-gyro',
        legendId: 'legend-gyro',
        title: 'Гироскоп Z',
        traces: [
            { label: 'gz', color: '#ff453a' },
        ],
        buf: new ChartBuffer(1),
        lastVals: [0],
    },
    ekf: {
        canvasId: 'chart-ekf',
        legendId: 'legend-ekf',
        title: 'EKF: Скорость',
        traces: [
            { label: 'speed_ms', color: '#5b8af5' },
            { label: 'slip_deg', color: '#ff9f0a' },
        ],
        buf: new ChartBuffer(2),
        lastVals: [0, 0],
    },
    ctrl: {
        canvasId: 'chart-ctrl',
        legendId: 'legend-ctrl',
        title: 'Управление',
        traces: [
            { label: 'cmd.thr', color: '#5b8af5' },
            { label: 'cmd.str', color: '#34c759' },
            { label: 'act.thr', color: '#ff453a' },
            { label: 'act.str', color: '#ff9f0a' },
        ],
        buf: new ChartBuffer(4),
        lastVals: [0, 0, 0, 0],
    },
};

let chartsPaused = false;
let chartsRafId = null;

function pushChartData(data) {
    if (chartsPaused) return;
    if (data.imu) {
        const d = chartDefs.accel;
        d.lastVals = [data.imu.ax || 0, data.imu.ay || 0, data.imu.az || 0];
        d.buf.push(d.lastVals);
        const dg = chartDefs.gyro;
        dg.lastVals = [data.imu.gz || 0];
        dg.buf.push(dg.lastVals);
    }
    if (data.ekf) {
        const d = chartDefs.ekf;
        d.lastVals = [data.ekf.speed_ms || 0, data.ekf.slip_deg || 0];
        d.buf.push(d.lastVals);
    }
    if (data.cmd || data.act) {
        const d = chartDefs.ctrl;
        d.lastVals = [
            (data.cmd && data.cmd.throttle) || 0,
            (data.cmd && data.cmd.steering) || 0,
            (data.act && data.act.throttle) || 0,
            (data.act && data.act.steering) || 0,
        ];
        d.buf.push(d.lastVals);
    }
}

function drawChart(canvas, buf, traces, lastVals) {
    const dpr = window.devicePixelRatio || 1;
    const cssW = canvas.clientWidth;
    const cssH = canvas.clientHeight;
    if (cssW === 0 || cssH === 0) return;

    // Resize canvas backing store if needed
    const needW = Math.round(cssW * dpr);
    const needH = Math.round(cssH * dpr);
    if (canvas.width !== needW || canvas.height !== needH) {
        canvas.width = needW;
        canvas.height = needH;
    }

    const ctx = canvas.getContext('2d');
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    ctx.scale(dpr, dpr);

    const W = cssW;
    const H = cssH;
    const PAD_LEFT = 36;
    const PAD_RIGHT = 4;
    const PAD_TOP = 4;
    const PAD_BOTTOM = 4;
    const plotW = W - PAD_LEFT - PAD_RIGHT;
    const plotH = H - PAD_TOP - PAD_BOTTOM;

    const nPoints = buf.len;
    if (nPoints < 2) {
        ctx.setTransform(1, 0, 0, 1, 0, 0);
        return;
    }

    // Compute global min/max across all traces
    let gmin = Infinity, gmax = -Infinity;
    for (let t = 0; t < traces.length; t++) {
        const arr = buf.getTrace(t);
        for (let i = 0; i < arr.length; i++) {
            if (arr[i] < gmin) gmin = arr[i];
            if (arr[i] > gmax) gmax = arr[i];
        }
    }
    if (gmin === gmax) { gmin -= 1; gmax += 1; }
    const pad = (gmax - gmin) * 0.1;
    gmin -= pad; gmax += pad;

    // Grid
    const gridColor = 'rgba(45,50,60,0.8)';
    ctx.strokeStyle = gridColor;
    ctx.lineWidth = 0.5;
    const gridLines = 4;
    for (let g = 0; g <= gridLines; g++) {
        const y = PAD_TOP + (g / gridLines) * plotH;
        ctx.beginPath();
        ctx.moveTo(PAD_LEFT, y);
        ctx.lineTo(PAD_LEFT + plotW, y);
        ctx.stroke();
    }

    // Y axis labels
    ctx.fillStyle = 'rgba(139,143,154,0.9)';
    ctx.font = `${Math.round(9 * dpr) / dpr}px -apple-system, system-ui, sans-serif`;
    ctx.textAlign = 'right';
    ctx.textBaseline = 'middle';
    const fmtY = (v) => {
        const a = Math.abs(v);
        if (a >= 100) return v.toFixed(0);
        if (a >= 10) return v.toFixed(1);
        return v.toFixed(2);
    };
    ctx.fillText(fmtY(gmax), PAD_LEFT - 2, PAD_TOP);
    ctx.fillText(fmtY(gmin), PAD_LEFT - 2, PAD_TOP + plotH);

    // Clip to plot area
    ctx.save();
    ctx.beginPath();
    ctx.rect(PAD_LEFT, PAD_TOP, plotW, plotH);
    ctx.clip();

    // Draw traces
    ctx.lineWidth = 1.5;
    ctx.imageSmoothingEnabled = true;
    const range = gmax - gmin;

    for (let t = 0; t < traces.length; t++) {
        const arr = buf.getTrace(t);
        ctx.strokeStyle = traces[t].color;
        ctx.beginPath();
        for (let i = 0; i < arr.length; i++) {
            const x = PAD_LEFT + (i / (CHART_WINDOW - 1)) * plotW;
            const y = PAD_TOP + plotH - ((arr[i] - gmin) / range) * plotH;
            if (i === 0) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
        }
        ctx.stroke();
    }

    ctx.restore();
    ctx.setTransform(1, 0, 0, 1, 0, 0);
}

function updateChartLegend(def) {
    const el = document.getElementById(def.legendId);
    if (!el) return;
    if (el.children.length === 0) {
        // Build legend items once
        for (let i = 0; i < def.traces.length; i++) {
            const item = document.createElement('span');
            item.className = 'chart-legend-item';
            item.innerHTML =
                `<span class="legend-swatch" style="background:${def.traces[i].color}"></span>` +
                `<span class="legend-name">${def.traces[i].label}</span>` +
                `\u00a0<span class="legend-val" id="lv-${def.canvasId}-${i}">—</span>`;
            el.appendChild(item);
        }
    }
    for (let i = 0; i < def.traces.length; i++) {
        const valEl = document.getElementById(`lv-${def.canvasId}-${i}`);
        if (valEl) {
            const v = def.lastVals[i];
            const a = Math.abs(v);
            valEl.textContent = a >= 100 ? v.toFixed(1) : a >= 10 ? v.toFixed(2) : v.toFixed(3);
        }
    }
}

function chartsRafLoop() {
    const panel = $('panel-charts');
    if (panel && panel.classList.contains('open')) {
        for (const key in chartDefs) {
            const def = chartDefs[key];
            const canvas = $(def.canvasId);
            if (canvas) drawChart(canvas, def.buf, def.traces, def.lastVals);
            updateChartLegend(def);
        }
    }
    chartsRafId = requestAnimationFrame(chartsRafLoop);
}

// Init pause button and RAF loop after DOM ready
(function initCharts() {
    const btnPause = $('btn-charts-pause');
    if (btnPause) {
        btnPause.addEventListener('click', () => {
            chartsPaused = !chartsPaused;
            btnPause.textContent = chartsPaused ? 'Продолжение' : 'Пауза';
            btnPause.classList.toggle('btn-accent', chartsPaused);
            btnPause.classList.toggle('btn-outline', !chartsPaused);
        });
    }

    // Set canvas CSS class for sizing
    for (const key in chartDefs) {
        const canvas = $(chartDefs[key].canvasId);
        if (canvas) canvas.className = 'chart-canvas';
    }

    chartsRafLoop();
})();

// ═══════════════════════════════════════════════════════════════════
// Telemetry display
// ═══════════════════════════════════════════════════════════════════

function updateTelem(data) {
    lastTelemTime = Date.now();
    telemRxCount++;
    if (telemCounterEl) telemCounterEl.textContent = `rx: ${telemRxCount}`;
    pushChartData(data);
    // Uptime display (reboot diagnostics)
    if (data.uptime_ms !== undefined && uptimeBadgeEl) {
        const totalSec = Math.floor(data.uptime_ms / 1000);
        const h = Math.floor(totalSec / 3600);
        const m = Math.floor((totalSec % 3600) / 60);
        const s = totalSec % 60;
        const pad = (n) => String(n).padStart(2, '0');
        uptimeBadgeEl.textContent = h > 0
            ? `⏱ ${h}:${pad(m)}:${pad(s)}`
            : `⏱ ${pad(m)}:${pad(s)}`;
    }

    if (data.mcu_pong_ok !== undefined) {
        setMcuStatus(data.mcu_pong_ok ? 'connected' : 'disconnected');
    } else {
        setMcuStatus('connected');
    }

    let html = '';
    const row = (label, val) =>
        `<div class="telem-item"><span class="telem-label">${label}</span><span class="telem-value">${val}</span></div>`;

    if (data.imu) {
        html += row('Accel X', data.imu.ax?.toFixed(2) ?? 'N/A');
        html += row('Accel Y', data.imu.ay?.toFixed(2) ?? 'N/A');
        html += row('Accel Z', data.imu.az?.toFixed(2) ?? 'N/A');
        html += row('Gyro X', data.imu.gx?.toFixed(2) ?? 'N/A');
        html += row('Gyro Y', data.imu.gy?.toFixed(2) ?? 'N/A');
        html += row('Gyro Z', data.imu.gz?.toFixed(2) ?? 'N/A');
    }
    if (data.rc) {
        html += row('RC Throttle', data.rc.throttle?.toFixed(2) ?? 'N/A');
        html += row('RC Steering', data.rc.steering?.toFixed(2) ?? 'N/A');
    }
    if (data.cmd) {
        html += row('Cmd Throttle', data.cmd.throttle?.toFixed(2) ?? 'N/A');
        html += row('Cmd Steering', data.cmd.steering?.toFixed(2) ?? 'N/A');
    }
    if (data.act) {
        html += row('Throttle', data.act.throttle?.toFixed(2) ?? 'N/A');
        html += row('Steering', data.act.steering?.toFixed(2) ?? 'N/A');
    }
    if (data.ekf) {
        html += row('EKF Slip', (data.ekf.slip_deg?.toFixed(1) ?? 'N/A') + ' °');
        html += row('EKF Speed', (data.ekf.speed_ms?.toFixed(2) ?? 'N/A') + ' m/s');
        html += row('EKF Vx/Vy', `${data.ekf.vx?.toFixed(2) ?? '?'} / ${data.ekf.vy?.toFixed(2) ?? '?'} m/s`);
        html += row('EKF σ²(vx/vy/r)', `${data.ekf.vx_var?.toExponential(1) ?? '?'} / ${data.ekf.vy_var?.toExponential(1) ?? '?'} / ${data.ekf.r_var?.toExponential(1) ?? '?'}`);
    }
    if (data.imu?.orientation) {
        const o = data.imu.orientation;
        html += row('Pitch', (o.pitch?.toFixed(1) ?? 'N/A') + ' °');
        html += row('Roll', (o.roll?.toFixed(1) ?? 'N/A') + ' °');
        html += row('Yaw', (o.yaw?.toFixed(1) ?? 'N/A') + ' °');
    }

    telemDataEl.innerHTML = html || '<p class="placeholder">Нет данных</p>';

    if (data.calib) updateCalibStatus(data.calib.status, data.calib);

    // Oversteer indicator
    const owIndicator = $('oversteer-indicator');
    const owStatus = $('oversteer-status');
    if (owIndicator && owStatus && data.warn !== undefined) {
        owIndicator.style.display = 'flex';
        if (data.warn.oversteer) {
            owStatus.textContent = 'ЗАНОС!';
            owStatus.className = 'badge badge-oversteer-active';
        } else {
            owStatus.textContent = 'OK';
            owStatus.className = 'badge badge-oversteer-ok';
        }
    }
}

// ═══════════════════════════════════════════════════════════════════
// Stabilization
// ═══════════════════════════════════════════════════════════════════

let currentMode = 0;
let kidsThrottleLimit = 0.50;
let kidsSteeringLimit = 0.70;
let kidsMode = false;

function applyStabConfig(cfg) {
    const set = (id, val) => { const el = $(id); if (el) el.value = val; };
    const setChk = (id, val) => { const el = $(id); if (el) el.checked = !!val; };

    currentMode = cfg.mode ?? 0;
    updateModeButtons(currentMode);

    setChk('stab-enabled', cfg.enabled);

    const pid = cfg.yaw_rate?.pid;
    set('stab-kp', pid?.kp ?? '');
    set('stab-ki', pid?.ki ?? '');
    set('stab-kd', pid?.kd ?? '');
    set('stab-max-corr', pid?.max_correction ?? '');
    setChk('adapt-pid-enabled', cfg.adaptive?.enabled);
    set('adapt-speed-ref', cfg.adaptive?.speed_ref_ms ?? '');
    setChk('madgwick-enabled', cfg.filter?.madgwick_enabled ?? true);
    setChk('ekf-enabled', cfg.filter?.ekf_enabled ?? true);
    setChk('adaptive-beta-enabled', cfg.filter?.adaptive_beta_enabled ?? true);
    set('adaptive-accel-thresh', cfg.filter?.adaptive_accel_threshold_g ?? '');
    setChk('pitch-comp-enabled', cfg.pitch_comp?.enabled);
    set('pitch-gain', cfg.pitch_comp?.gain ?? '');
    set('pitch-max-corr', cfg.pitch_comp?.max_correction ?? '');
    setChk('oversteer-enabled', cfg.oversteer?.warn_enabled);
    set('ow-slip-thresh', cfg.oversteer?.slip_thresh_deg ?? '');
    set('ow-rate-thresh', cfg.oversteer?.rate_thresh_deg_s ?? '');
    set('ow-throttle-red', cfg.oversteer?.throttle_reduction ?? '');

    // Slew rate
    {
        const ss = cfg.slew_steering ?? 3.0;
        const st = cfg.slew_throttle ?? 0.5;
        set('slew-steering', ss);
        set('slew-throttle', st);
        const ssEl = $('slew-steering-value');
        const stEl = $('slew-throttle-value');
        if (ssEl) ssEl.textContent = ss.toFixed(1);
        if (stEl) stEl.textContent = st.toFixed(1);
    }

    // Braking
    {
        const bm = cfg.braking_mode ?? 0;
        const mult = cfg.brake_slew_multiplier ?? 4.0;
        const coastEl = $('braking-coast');
        const brakeEl = $('braking-brake');
        if (coastEl) coastEl.checked = (bm === 0);
        if (brakeEl) brakeEl.checked = (bm === 1);
        set('brake-slew-mult', mult);
        const multEl = $('brake-slew-mult-value');
        if (multEl) multEl.textContent = parseFloat(mult).toFixed(1) + '×';
        const rowEl = $('brake-multiplier-row');
        if (rowEl) rowEl.style.display = (bm === 1) ? 'flex' : 'none';
    }

    // Trim
    setTrimValue('steering-trim', cfg.steering_trim ?? 0);
    setTrimValue('throttle-trim', cfg.throttle_trim ?? 0);

    const km = cfg.kids_mode;
    if (km) {
        const tPct = Math.round((km.throttle_limit ?? 0.5) * 100);
        const sPct = Math.round((km.steering_limit ?? 0.7) * 100);
        kidsThrottleLimit = km.throttle_limit ?? 0.5;
        kidsSteeringLimit = km.steering_limit ?? 0.7;
        set('kids-throttle-limit', tPct);
        set('kids-steering-limit', sPct);
        const tEl = $('kids-throttle-value');
        const sEl = $('kids-steering-value');
        if (tEl) tEl.textContent = tPct;
        if (sEl) sEl.textContent = sPct;

        const speedEnabled = km.speed_limit_enabled ?? false;
        const maxSpeedMs = km.max_speed_ms ?? 1.5;
        const chk = $('kids-speed-limit-enabled');
        if (chk) chk.checked = speedEnabled;
        const sliderVal = Math.round(maxSpeedMs * 10);
        set('kids-max-speed', sliderVal);
        const spEl = $('kids-max-speed-value');
        if (spEl) spEl.textContent = maxSpeedMs.toFixed(1);
        const speedRow = $('kids-speed-row');
        if (speedRow) speedRow.style.display = speedEnabled ? 'flex' : 'none';
    }
}

function updateModeButtons(mode) {
    const ids = ['btn-mode-normal','btn-mode-sport','btn-mode-drift','btn-mode-kids','btn-mode-direct'];
    ids.forEach((id, idx) => {
        const el = $(id);
        if (!el) return;
        el.classList.toggle('active', idx === mode);
    });

    const directBanner = $('direct-law-banner');
    const controlPanel = $('panel-control');
    if (mode === 4) {
        if (directBanner) directBanner.style.display = 'block';
        if (controlPanel) controlPanel.classList.add('direct-active');
    } else {
        if (directBanner) directBanner.style.display = 'none';
        if (controlPanel) controlPanel.classList.remove('direct-active');
    }

    updateKidsModeUI();
}

function updateKidsModeUI() {
    const kidsSettings = $('kids-mode-settings');
    const kidsBanner = $('kids-mode-banner');
    const controlPanel = $('panel-control');
    const toggleBtn = $('btn-kids-toggle');

    const active = kidsMode || currentMode === 3;
    if (kidsSettings) kidsSettings.style.display = active ? 'block' : 'none';
    if (kidsBanner) kidsBanner.style.display = active ? 'block' : 'none';
    if (controlPanel) controlPanel.classList.toggle('kids-active', active);
    if (toggleBtn) {
        toggleBtn.textContent = 'Детский режим: ' + (active ? 'ВКЛ' : 'ВЫКЛ');
        toggleBtn.classList.toggle('btn-kids-active', active);
    }
}

function loadStabConfig() { wsSend({ type: 'get_stab_config' }); }

function saveStabConfig() {
    const getF = (id) => { const el = $(id); return el ? parseFloat(el.value) : undefined; };
    const getChk = (id) => { const el = $(id); return el ? el.checked : false; };

    wsSend({
        type: 'set_stab_config',
        mode: currentMode,
        enabled: getChk('stab-enabled'),
        filter: {
            madgwick_enabled: getChk('madgwick-enabled'),
            ekf_enabled: getChk('ekf-enabled'),
            adaptive_beta_enabled: getChk('adaptive-beta-enabled'),
            adaptive_accel_threshold_g: getF('adaptive-accel-thresh'),
        },
        yaw_rate: { pid: {
            kp: getF('stab-kp'), ki: getF('stab-ki'), kd: getF('stab-kd'),
            max_correction: getF('stab-max-corr'),
        }},
        adaptive: { enabled: getChk('adapt-pid-enabled'), speed_ref_ms: getF('adapt-speed-ref') },
        pitch_comp: { enabled: getChk('pitch-comp-enabled'), gain: getF('pitch-gain'), max_correction: getF('pitch-max-corr') },
        oversteer: {
            warn_enabled: getChk('oversteer-enabled'),
            slip_thresh_deg: getF('ow-slip-thresh'),
            rate_thresh_deg_s: getF('ow-rate-thresh'),
            throttle_reduction: getF('ow-throttle-red'),
        },
        kids_mode: {
            throttle_limit: kidsThrottleLimit,
            reverse_limit: kidsThrottleLimit,
            steering_limit: kidsSteeringLimit,
            speed_limit_enabled: $('kids-speed-limit-enabled')?.checked ?? false,
            max_speed_ms: ($('kids-max-speed') ? parseInt($('kids-max-speed').value) / 10.0 : 1.5),
        },
        slew_steering: getF('slew-steering'),
        slew_throttle: getF('slew-throttle'),
        braking_mode: $('braking-brake')?.checked ? 1 : 0,
        brake_slew_multiplier: getF('brake-slew-mult'),
        steering_trim: getF('steering-trim'),
        throttle_trim: getF('throttle-trim'),
    });
}

function showStabSaveStatus(msg, cssClass) {
    const el = $('stab-save-status');
    const msgEl = $('stab-save-msg');
    if (!el || !msgEl) return;
    el.style.display = 'block';
    el.className = 'toast ' + cssClass;
    msgEl.textContent = msg;
    setTimeout(() => { el.style.display = 'none'; }, 3000);
}

// ═══════════════════════════════════════════════════════════════════
// Telemetry log
// ═══════════════════════════════════════════════════════════════════

let pendingLogFrames = [];
let pendingLogTotal = 0;
let pendingLogOffset = 0;

function updateLogInfo(count, capacity) {
    const cEl = $('log-count');
    const capEl = $('log-capacity');
    if (cEl) cEl.textContent = count ?? '—';
    if (capEl) capEl.textContent = capacity ?? '—';
}

function handleLogData(frames) {
    if (!Array.isArray(frames)) return;
    pendingLogFrames = pendingLogFrames.concat(frames);
    if (pendingLogTotal > 0) {
        if (pendingLogFrames.length >= pendingLogTotal) {
            exportLogCsv(pendingLogFrames);
            pendingLogFrames = [];
            pendingLogTotal = 0;
        } else {
            const nextOff = pendingLogOffset + pendingLogFrames.length;
            const rem = pendingLogTotal - pendingLogFrames.length;
            wsSend({ type: 'get_log_data', offset: nextOff, count: Math.min(200, rem) });
        }
    }
}

function exportLogCsv(frames) {
    if (!frames || !frames.length) { alert('Нет данных'); return; }
    const hdr = 'ts_ms,ax,ay,az,gx,gy,gz,vx,vy,slip_deg,speed_ms,throttle,steering,pitch_deg,roll_deg,yaw_deg,yaw_rate_dps,oversteer_active,rc_throttle,rc_steering,cmd_throttle,cmd_steering,ekf_vx_var,ekf_vy_var,ekf_r_var,test_marker\n';
    const rows = frames.map(f =>
        `${f.ts_ms},${f.ax},${f.ay},${f.az},${f.gx},${f.gy},${f.gz},${f.vx},${f.vy},${f.slip_deg},${f.speed_ms},${f.throttle},${f.steering},${f.pitch_deg},${f.roll_deg},${f.yaw_deg},${f.yaw_rate_dps},${f.oversteer_active},${f.rc_throttle},${f.rc_steering},${f.cmd_throttle??0},${f.cmd_steering??0},${f.ekf_vx_var??0},${f.ekf_vy_var??0},${f.ekf_r_var??0},${f.test_marker??0}`
    ).join('\n');
    const blob = new Blob([hdr + rows], { type: 'text/csv' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url; a.download = 'telemetry_log.csv'; a.click();
    URL.revokeObjectURL(url);
}

// ─────────────────────────────────────────────────────────────
// Binary log download via HTTP (replaces slow WebSocket JSON pagination)
// Protocol: [4B frame_count][4B frame_size][count × frame_size raw bytes]
// ─────────────────────────────────────────────────────────────

async function downloadBinaryLog() {
    const btn = $('btn-log-csv');
    if (btn) { btn.disabled = true; btn.textContent = 'Скачивание...'; }
    try {
        const resp = await fetch('/api/log.bin', { cache: 'no-store' });
        if (!resp.ok) { alert('Ошибка: ' + resp.status); return; }
        const buf = await resp.arrayBuffer();
        const view = new DataView(buf);
        if (buf.byteLength < 8) { alert('Нет данных'); return; }
        const frameCount = view.getUint32(0, true);
        const frameSize = view.getUint32(4, true);
        if (frameCount === 0) { alert('Нет данных'); return; }

        const FIELD_OFFSETS = [
            { name: 'ts_ms',   off: 0, type: 'u32' },
            { name: 'ax',      off: 4, type: 'f32' },
            { name: 'ay',      off: 8, type: 'f32' },
            { name: 'az',      off: 12, type: 'f32' },
            { name: 'gx',      off: 16, type: 'f32' },
            { name: 'gy',      off: 20, type: 'f32' },
            { name: 'gz',      off: 24, type: 'f32' },
            { name: 'vx',      off: 28, type: 'f32' },
            { name: 'vy',      off: 32, type: 'f32' },
            { name: 'slip_deg',off: 36, type: 'f32' },
            { name: 'speed_ms',off: 40, type: 'f32' },
            { name: 'throttle',off: 44, type: 'f32' },
            { name: 'steering',off: 48, type: 'f32' },
            { name: 'pitch_deg',off: 52, type: 'f32' },
            { name: 'roll_deg', off: 56, type: 'f32' },
            { name: 'yaw_deg',  off: 60, type: 'f32' },
            { name: 'yaw_rate_dps', off: 64, type: 'f32' },
            { name: 'oversteer_active', off: 68, type: 'f32' },
            { name: 'rc_throttle', off: 72, type: 'f32' },
            { name: 'rc_steering', off: 76, type: 'f32' },
            { name: 'cmd_throttle', off: 80, type: 'f32' },
            { name: 'cmd_steering', off: 84, type: 'f32' },
            { name: 'ekf_vx_var',  off: 88, type: 'f32' },
            { name: 'ekf_vy_var',  off: 92, type: 'f32' },
            { name: 'ekf_r_var',   off: 96, type: 'f32' },
            { name: 'test_marker', off: 100, type: 'u8' },
        ];

        const hdr = FIELD_OFFSETS.map(f => f.name).join(',') + '\n';
        const lines = [];
        const dataOffset = 8; // after header
        for (let i = 0; i < frameCount; i++) {
            const base = dataOffset + i * frameSize;
            if (base + frameSize > buf.byteLength) break;
            const vals = FIELD_OFFSETS.map(f => {
                const o = base + f.off;
                if (f.type === 'u32') return view.getUint32(o, true);
                if (f.type === 'u8') return view.getUint8(o);
                return view.getFloat32(o, true);
            });
            lines.push(vals.join(','));
        }
        const csv = hdr + lines.join('\n');
        const blob = new Blob([csv], { type: 'text/csv' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url; a.download = 'telemetry_log.csv'; a.click();
        URL.revokeObjectURL(url);
    } catch (e) {
        alert('Ошибка скачивания: ' + e.message);
    } finally {
        if (btn) { btn.disabled = false; btn.textContent = 'CSV'; }
    }
}

// ═══════════════════════════════════════════════════════════════════
// Self-Test
// ═══════════════════════════════════════════════════════════════════

const btnSelfTest = $('btn-self-test');
const selfTestResultDiv = $('self-test-result');
const selfTestOverall = $('self-test-overall');
const selfTestTableBody = document.querySelector('#self-test-table tbody');

function showSelfTestResult(data) {
    if (btnSelfTest) { btnSelfTest.disabled = false; btnSelfTest.textContent = 'Запустить'; }
    selfTestResultDiv.style.display = 'block';
    selfTestOverall.textContent = data.passed ? 'ALL PASS' : 'FAIL';
    selfTestOverall.className = 'badge ' + (data.passed ? 'badge-on' : 'badge-off');

    selfTestTableBody.innerHTML = '';
    if (data.tests) {
        for (const t of data.tests) {
            const row = document.createElement('tr');
            row.innerHTML = `<td>${t.name}</td><td class="${t.passed ? 'st-pass' : 'st-fail'}">${t.passed ? 'PASS' : 'FAIL'}</td><td>${t.value || ''}</td>`;
            selfTestTableBody.appendChild(row);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════
// Trim controls
// ═══════════════════════════════════════════════════════════════════

function setTrimValue(id, val) {
    val = Math.max(-0.1, Math.min(0.1, parseFloat(val) || 0));
    const hidden = $(id);
    const display = $(id + '-value');
    if (hidden) hidden.value = val;
    if (display) display.textContent = val.toFixed(2);
}

document.querySelectorAll('.btn-trim').forEach(btn => {
    btn.addEventListener('click', () => {
        const targetId = btn.dataset.target;
        const step = parseFloat(btn.dataset.step);
        const hidden = $(targetId);
        if (!hidden) return;
        const cur = parseFloat(hidden.value) || 0;
        setTrimValue(targetId, cur + step);
    });
});

// ═══════════════════════════════════════════════════════════════════
// Event listeners
// ═══════════════════════════════════════════════════════════════════

throttleSlider.addEventListener('input', (e) => { throttleValueEl.textContent = parseFloat(e.target.value).toFixed(2); });
steeringSlider.addEventListener('input', (e) => { steeringValueEl.textContent = parseFloat(e.target.value).toFixed(2); });

btnCenter.addEventListener('click', () => {
    throttleSlider.value = 0; steeringSlider.value = 0;
    throttleValueEl.textContent = '0.00'; steeringValueEl.textContent = '0.00';
});
btnStop.addEventListener('click', () => { throttleSlider.value = 0; throttleValueEl.textContent = '0.00'; });

// Calibration
if (btnCalibGyro)  btnCalibGyro.addEventListener('click', () => sendCalibrate('gyro'));
if (btnCalibFull)  btnCalibFull.addEventListener('click', () => sendCalibrate('full'));
if (btnCalibForward) btnCalibForward.addEventListener('click', () => sendCalibrate('forward'));
// Steering trim calibration
function updateTrimCalibStatus(data) {
    if (!trimCalibStatusEl) return;
    trimCalibStatusEl.style.display = 'block';
    if (data.status === 'started' || data.active) {
        trimCalibStatusEl.textContent = 'Калибровка trim идёт...';
        if (btnCalibTrim) btnCalibTrim.disabled = true;
        // Poll status
        if (data.active) setTimeout(() => wsSend({ type: 'get_steering_trim_status' }), 1000);
    } else if (data.result && data.result.valid) {
        trimCalibStatusEl.textContent = `Trim = ${data.result.trim.toFixed(4)}, yaw drift = ${data.result.mean_yaw_rate.toFixed(2)} dps (${data.result.samples} samples)`;
        if (btnCalibTrim) btnCalibTrim.disabled = false;
        // Refresh config to show new trim value
        wsSend({ type: 'get_stab_config' });
    } else if (data.result && !data.result.valid) {
        trimCalibStatusEl.textContent = `Не удалось: yaw = ${data.result.mean_yaw_rate?.toFixed(1) ?? '?'} dps (${data.result.samples ?? 0} samples)`;
        if (btnCalibTrim) btnCalibTrim.disabled = false;
    } else if (data.ok === false) {
        trimCalibStatusEl.textContent = data.error || 'Ошибка запуска';
        if (btnCalibTrim) btnCalibTrim.disabled = false;
    }
}
if (btnCalibTrim) btnCalibTrim.addEventListener('click', () => {
    let target_accel = 0.1;
    if (calibFwdAccelSlider) {
        target_accel = parseInt(calibFwdAccelSlider.value) / 1000;
    }
    wsSend({ type: 'calibrate_steering_trim', target_accel });
    if (trimCalibStatusEl) {
        trimCalibStatusEl.style.display = 'block';
        trimCalibStatusEl.textContent = 'Запуск...';
    }
    // Start polling after a delay
    setTimeout(() => wsSend({ type: 'get_steering_trim_status' }), 2000);
});

if (btnCalibAutoFwd) btnCalibAutoFwd.addEventListener('click', () => {
    let target_accel = 0.1;
    if (calibFwdAccelSlider) {
        target_accel = parseInt(calibFwdAccelSlider.value) / 1000;
    } else if (calibFwdThrottleSlider) {
        target_accel = parseInt(calibFwdThrottleSlider.value) / 100 * 0.4;
    }
    wsSend({ type: 'calibrate_imu', mode: 'auto_forward', target_accel });
});
if (calibFwdAccelSlider) {
    calibFwdAccelSlider.addEventListener('input', (e) => { if (calibFwdAccelValueEl) calibFwdAccelValueEl.textContent = e.target.value; });
}
if (calibFwdThrottleSlider) {
    calibFwdThrottleSlider.addEventListener('input', (e) => { if (calibFwdThrottleValueEl) calibFwdThrottleValueEl.textContent = e.target.value; });
}

// ── CoM Offset Calibration ──
const btnCalibCom       = $('btn-calib-com');
const comCalibStatusEl  = $('com-calib-status');
const comSteeringSlider = $('com-steering');
const comSteeringValueEl = $('com-steering-value');
const comDurationSlider = $('com-duration');
const comDurationValueEl = $('com-duration-value');

function updateComCalibStatus(data) {
    if (!comCalibStatusEl) return;
    comCalibStatusEl.style.display = 'block';
    if (data.status === 'started' || data.active) {
        comCalibStatusEl.textContent = 'Калибровка CoM идёт...';
        if (btnCalibCom) btnCalibCom.disabled = true;
        if (data.active) setTimeout(() => wsSend({ type: 'get_com_offset_status' }), 1000);
    } else if (data.result && data.result.valid) {
        comCalibStatusEl.textContent = `CoM offset: rx=${data.result.rx.toFixed(4)}m ry=${data.result.ry.toFixed(4)}m ` +
            `(ω_cw=${data.result.omega_cw_dps.toFixed(1)} ω_ccw=${data.result.omega_ccw_dps.toFixed(1)} dps, ` +
            `${data.result.samples_cw}+${data.result.samples_ccw} samples)`;
        if (btnCalibCom) btnCalibCom.disabled = false;
    } else if (data.result && !data.result.valid) {
        comCalibStatusEl.textContent = `CoM калибровка не удалась (${data.result.samples_cw}+${data.result.samples_ccw} samples)`;
        if (btnCalibCom) btnCalibCom.disabled = false;
    } else if (data.ok === false) {
        comCalibStatusEl.textContent = data.error || 'Ошибка запуска';
        if (btnCalibCom) btnCalibCom.disabled = false;
    }
}

if (comSteeringSlider) comSteeringSlider.addEventListener('input', (e) => { if (comSteeringValueEl) comSteeringValueEl.textContent = parseFloat(e.target.value).toFixed(2); });
if (comDurationSlider) comDurationSlider.addEventListener('input', (e) => { if (comDurationValueEl) comDurationValueEl.textContent = parseFloat(e.target.value).toFixed(1); });

if (btnCalibCom) btnCalibCom.addEventListener('click', () => {
    let target_accel = 0.1;
    if (calibFwdAccelSlider) target_accel = parseInt(calibFwdAccelSlider.value) / 1000;
    const steering = comSteeringSlider ? parseFloat(comSteeringSlider.value) : 0.5;
    const duration = comDurationSlider ? parseFloat(comDurationSlider.value) : 5.0;
    wsSend({ type: 'calibrate_com_offset', target_accel, steering, duration });
    if (comCalibStatusEl) { comCalibStatusEl.style.display = 'block'; comCalibStatusEl.textContent = 'Запуск...'; }
    setTimeout(() => wsSend({ type: 'get_com_offset_status' }), 2000);
});

// ── Test Maneuvers ──
const testTypeSelect    = $('test-type');
const testAccelSlider   = $('test-accel');
const testAccelValueEl  = $('test-accel-value');
const testDurSlider     = $('test-duration');
const testDurValueEl    = $('test-duration-value');
const testSteerSlider   = $('test-steering');
const testSteerValueEl  = $('test-steering-value');
const testSteerGroup    = $('test-steering-group');
const btnTestStart      = $('btn-test-start');
const btnTestStop       = $('btn-test-stop');
const testRunStatusEl   = $('test-run-status');
const testStatusBadgeEl = $('test-status-badge');

function updateTestStatus(data) {
    if (!testRunStatusEl) return;
    testRunStatusEl.style.display = 'block';

    if (data.type === 'start_test_ack') {
        if (data.ok) {
            testRunStatusEl.textContent = `Тест "${data.test_type}" запущен`;
            if (btnTestStart) btnTestStart.disabled = true;
            if (testStatusBadgeEl) { testStatusBadgeEl.textContent = 'RUN'; testStatusBadgeEl.className = 'badge badge-warn'; }
            setTimeout(() => wsSend({ type: 'get_test_status' }), 1000);
        } else {
            testRunStatusEl.textContent = data.error || 'Ошибка запуска';
            if (testStatusBadgeEl) { testStatusBadgeEl.textContent = 'ERR'; testStatusBadgeEl.className = 'badge badge-off'; }
        }
        return;
    }

    if (data.type === 'stop_test_ack') {
        testRunStatusEl.textContent = 'Тест остановлен';
        if (btnTestStart) btnTestStart.disabled = false;
        if (testStatusBadgeEl) { testStatusBadgeEl.textContent = 'STOP'; testStatusBadgeEl.className = 'badge badge-off'; }
        return;
    }

    // test_status
    if (data.active) {
        const phaseNames = { accelerate: 'Разгон', cruise: 'Круиз', step_exec: 'Step', brake: 'Торможение' };
        testRunStatusEl.textContent = `${data.test_type}: ${phaseNames[data.phase] || data.phase} (${data.elapsed.toFixed(1)}с)`;
        if (btnTestStart) btnTestStart.disabled = true;
        if (testStatusBadgeEl) { testStatusBadgeEl.textContent = data.phase.toUpperCase(); testStatusBadgeEl.className = 'badge badge-warn'; }
        setTimeout(() => wsSend({ type: 'get_test_status' }), 500);
    } else if (data.phase === 'done') {
        testRunStatusEl.textContent = `Тест завершён (${data.elapsed.toFixed(1)}с)`;
        if (btnTestStart) btnTestStart.disabled = false;
        if (testStatusBadgeEl) { testStatusBadgeEl.textContent = 'DONE'; testStatusBadgeEl.className = 'badge badge-on'; }
    } else if (data.phase === 'failed') {
        testRunStatusEl.textContent = `Тест прерван`;
        if (btnTestStart) btnTestStart.disabled = false;
        if (testStatusBadgeEl) { testStatusBadgeEl.textContent = 'FAIL'; testStatusBadgeEl.className = 'badge badge-off'; }
    } else {
        testRunStatusEl.textContent = '';
        testRunStatusEl.style.display = 'none';
        if (btnTestStart) btnTestStart.disabled = false;
        if (testStatusBadgeEl) { testStatusBadgeEl.textContent = '—'; testStatusBadgeEl.className = 'badge badge-unknown'; }
    }
}

if (testTypeSelect) testTypeSelect.addEventListener('change', () => {
    if (testSteerGroup) testSteerGroup.style.display = (testTypeSelect.value === 'straight') ? 'none' : '';
});
if (testAccelSlider) testAccelSlider.addEventListener('input', (e) => { if (testAccelValueEl) testAccelValueEl.textContent = e.target.value; });
if (testDurSlider) testDurSlider.addEventListener('input', (e) => { if (testDurValueEl) testDurValueEl.textContent = parseFloat(e.target.value).toFixed(1); });
if (testSteerSlider) testSteerSlider.addEventListener('input', (e) => { if (testSteerValueEl) testSteerValueEl.textContent = parseFloat(e.target.value).toFixed(2); });

if (btnTestStart) btnTestStart.addEventListener('click', () => {
    const target_accel = testAccelSlider ? parseInt(testAccelSlider.value) / 1000 : 0.1;
    const duration = testDurSlider ? parseFloat(testDurSlider.value) : 3.0;
    const steering = testSteerSlider ? parseFloat(testSteerSlider.value) : 0.3;
    const test_type = testTypeSelect ? testTypeSelect.value : 'straight';
    wsSend({ type: 'start_test', test_type, target_accel, duration, steering });
    if (testRunStatusEl) { testRunStatusEl.style.display = 'block'; testRunStatusEl.textContent = 'Запуск...'; }
});

if (btnTestStop) btnTestStop.addEventListener('click', () => {
    wsSend({ type: 'stop_test' });
});

// Wi-Fi scan list
if (staScanList) {
    staScanList.addEventListener('change', async () => {
        const ssid = (staScanList.value || '').trim();
        if (!ssid) return;
        if (staSsidInput) staSsidInput.value = ssid;

        const opt = staScanList.selectedOptions?.[0];
        const isOpen = opt?.dataset?.open === '1';

        if (isOpen) {
            if (staPassInput) staPassInput.value = '';
            try { await fetch('/api/wifi/sta/connect', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ ssid, password: '', save: true }) }); } catch (e) {}
            setTimeout(fetchWifiStatus, 300);
            return;
        }

        const defaultPass = staPassInput ? staPassInput.value : '';
        const pass = prompt(`Пароль для "${ssid}"`, defaultPass);
        if (pass === null) return;
        if (staPassInput) staPassInput.value = pass;
        try { await fetch('/api/wifi/sta/connect', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ ssid, password: pass, save: true }) }); } catch (e) {}
        setTimeout(fetchWifiStatus, 300);
    });
}

if (btnStaScan) btnStaScan.addEventListener('click', () => scanWifiNetworks());

btnStaConnect.addEventListener('click', async () => {
    const ssid = (staSsidInput?.value || '').trim();
    const password = staPassInput?.value || '';
    if (!ssid) return;
    try { await fetch('/api/wifi/sta/connect', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ ssid, password, save: true }) }); } catch (e) {}
    setTimeout(fetchWifiStatus, 300);
});

btnStaDisconnect.addEventListener('click', async () => {
    try { await fetch('/api/wifi/sta/disconnect', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ forget: false }) }); } catch (e) {}
    setTimeout(fetchWifiStatus, 300);
});

btnStaForget.addEventListener('click', async () => {
    try { await fetch('/api/wifi/sta/disconnect', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ forget: true }) }); } catch (e) {}
    if (staSsidInput) staSsidInput.value = '';
    if (staPassInput) staPassInput.value = '';
    setTimeout(fetchWifiStatus, 300);
});

// Stabilization buttons
const btnLoadStab = $('btn-load-stab');
const btnSaveStab = $('btn-save-stab');
if (btnLoadStab) btnLoadStab.addEventListener('click', loadStabConfig);
if (btnSaveStab) btnSaveStab.addEventListener('click', saveStabConfig);

const kidsThrottleSliderEl = $('kids-throttle-limit');
const kidsSteeringSliderEl = $('kids-steering-limit');
const kidsThrottleValueEl = $('kids-throttle-value');
const kidsSteeringValueEl = $('kids-steering-value');

const btnModeNormal = $('btn-mode-normal');
const btnModeSport  = $('btn-mode-sport');
const btnModeDrift  = $('btn-mode-drift');
const btnModeKids   = $('btn-mode-kids');
const btnModeDirect = $('btn-mode-direct');
const btnKidsToggle = $('btn-kids-toggle');

const setMode = (m) => { currentMode = m; updateModeButtons(m); wsSend({ type: 'set_stab_config', mode: m }); };
if (btnModeNormal) btnModeNormal.addEventListener('click', () => setMode(0));
if (btnModeSport)  btnModeSport.addEventListener('click',  () => setMode(1));
if (btnModeDrift)  btnModeDrift.addEventListener('click',  () => setMode(2));
if (btnModeKids)   btnModeKids.addEventListener('click',   () => setMode(3));
if (btnModeDirect) btnModeDirect.addEventListener('click', () => setMode(4));

if (btnKidsToggle) btnKidsToggle.addEventListener('click', () => {
    kidsMode = !kidsMode;
    updateKidsModeUI();
    wsSend({ type: 'toggle_kids_mode', active: kidsMode });
});

if (kidsThrottleSliderEl) kidsThrottleSliderEl.addEventListener('input', (e) => {
    const pct = parseInt(e.target.value);
    kidsThrottleLimit = pct / 100;
    if (kidsThrottleValueEl) kidsThrottleValueEl.textContent = pct;
});
if (kidsSteeringSliderEl) kidsSteeringSliderEl.addEventListener('input', (e) => {
    const pct = parseInt(e.target.value);
    kidsSteeringLimit = pct / 100;
    if (kidsSteeringValueEl) kidsSteeringValueEl.textContent = pct;
});

const kidsSpeedChkEl = $('kids-speed-limit-enabled');
const kidsSpeedSliderEl = $('kids-max-speed');
const kidsSpeedValueEl = $('kids-max-speed-value');
const kidsSpeedRowEl = $('kids-speed-row');
if (kidsSpeedChkEl) kidsSpeedChkEl.addEventListener('change', (e) => {
    if (kidsSpeedRowEl) kidsSpeedRowEl.style.display = e.target.checked ? 'flex' : 'none';
});
if (kidsSpeedSliderEl) kidsSpeedSliderEl.addEventListener('input', (e) => {
    const ms = parseInt(e.target.value) / 10.0;
    if (kidsSpeedValueEl) kidsSpeedValueEl.textContent = ms.toFixed(1);
});

// Slew rate sliders
const slewSteeringSliderEl = $('slew-steering');
const slewThrottleSliderEl = $('slew-throttle');
if (slewSteeringSliderEl) slewSteeringSliderEl.addEventListener('input', (e) => {
    const v = parseFloat(e.target.value);
    const el = $('slew-steering-value');
    if (el) el.textContent = v.toFixed(1);
});
if (slewThrottleSliderEl) slewThrottleSliderEl.addEventListener('input', (e) => {
    const v = parseFloat(e.target.value);
    const el = $('slew-throttle-value');
    if (el) el.textContent = v.toFixed(1);
});

// Braking controls
document.querySelectorAll('input[name="braking-mode"]').forEach(el => {
    el.addEventListener('change', () => {
        const rowEl = $('brake-multiplier-row');
        if (rowEl) rowEl.style.display = $('braking-brake')?.checked ? 'flex' : 'none';
    });
});
const brakeSlewSliderEl = $('brake-slew-mult');
if (brakeSlewSliderEl) brakeSlewSliderEl.addEventListener('input', (e) => {
    const v = parseFloat(e.target.value);
    const el = $('brake-slew-mult-value');
    if (el) el.textContent = v.toFixed(1) + '×';
});

// Log buttons
const btnLogInfo  = $('btn-log-info');
const btnLogClear = $('btn-log-clear');
const btnLogCsv   = $('btn-log-csv');
if (btnLogInfo)  btnLogInfo.addEventListener('click', () => wsSend({ type: 'get_log_info' }));
if (btnLogClear) btnLogClear.addEventListener('click', () => wsSend({ type: 'clear_log' }));
if (btnLogCsv)   btnLogCsv.addEventListener('click', () => downloadBinaryLog());

// Self-test
if (btnSelfTest) btnSelfTest.addEventListener('click', () => {
    btnSelfTest.disabled = true;
    btnSelfTest.textContent = 'Выполняется...';
    selfTestResultDiv.style.display = 'none';
    wsSend({ type: 'run_self_test' });
    setTimeout(() => { btnSelfTest.disabled = false; btnSelfTest.textContent = 'Запустить'; }, 5000);
});

// ═══════════════════════════════════════════════════════════════════
// Init
// ═══════════════════════════════════════════════════════════════════

window.addEventListener('load', () => {
    connectWebSocket();
    fetchWifiStatus();
    if (wifiStatusInterval) clearInterval(wifiStatusInterval);
    wifiStatusInterval = setInterval(fetchWifiStatus, 1000);
});

// Пробуждение экрана: немедленно переподключить WebSocket
document.addEventListener('visibilitychange', () => {
    if (document.visibilityState === 'visible') {
        // Страница снова видна (телефон проснулся / вкладка активна)
        if (!ws || ws.readyState !== WebSocket.OPEN) {
            connectWebSocket();
        }
        requestWakeLock();  // Wake Lock сбрасывается при засыпании — запросить снова
    }
});

window.addEventListener('beforeunload', () => {
    stopCommandSending();
    releaseWakeLock();
    if (wifiStatusInterval) { clearInterval(wifiStatusInterval); wifiStatusInterval = null; }
    if (ws) ws.close();
});
