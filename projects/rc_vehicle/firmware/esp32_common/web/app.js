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
                        const want = Math.min(500, total);
                        const offset = total > want ? total - want : 0;
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
// Telemetry display
// ═══════════════════════════════════════════════════════════════════

function updateTelem(data) {
    lastTelemTime = Date.now();
    telemRxCount++;
    if (telemCounterEl) telemCounterEl.textContent = `rx: ${telemRxCount}`;
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
    if (data.act) {
        html += row('Throttle', data.act.throttle?.toFixed(2) ?? 'N/A');
        html += row('Steering', data.act.steering?.toFixed(2) ?? 'N/A');
    }
    if (data.ekf) {
        html += row('EKF Slip', (data.ekf.slip_deg?.toFixed(1) ?? 'N/A') + ' °');
        html += row('EKF Speed', (data.ekf.speed_ms?.toFixed(2) ?? 'N/A') + ' m/s');
        html += row('EKF Vx/Vy', `${data.ekf.vx?.toFixed(2) ?? '?'} / ${data.ekf.vy?.toFixed(2) ?? '?'} m/s`);
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
    setChk('adaptive-beta-enabled', cfg.filter?.adaptive_beta_enabled ?? true);
    set('adaptive-accel-thresh', cfg.filter?.adaptive_accel_threshold_g ?? '');
    setChk('pitch-comp-enabled', cfg.pitch_comp?.enabled);
    set('pitch-gain', cfg.pitch_comp?.gain ?? '');
    set('pitch-max-corr', cfg.pitch_comp?.max_correction ?? '');
    setChk('oversteer-enabled', cfg.oversteer?.warn_enabled);
    set('ow-slip-thresh', cfg.oversteer?.slip_thresh_deg ?? '');
    set('ow-rate-thresh', cfg.oversteer?.rate_thresh_deg_s ?? '');
    set('ow-throttle-red', cfg.oversteer?.throttle_reduction ?? '');

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
        },
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
    const hdr = 'ts_ms,ax,ay,az,gx,gy,gz,vx,vy,slip_deg,speed_ms,throttle,steering,pitch_deg,roll_deg,yaw_deg,yaw_rate_dps,oversteer_active\n';
    const rows = frames.map(f =>
        `${f.ts_ms},${f.ax},${f.ay},${f.az},${f.gx},${f.gy},${f.gz},${f.vx},${f.vy},${f.slip_deg},${f.speed_ms},${f.throttle},${f.steering},${f.pitch_deg},${f.roll_deg},${f.yaw_deg},${f.yaw_rate_dps},${f.oversteer_active}`
    ).join('\n');
    const blob = new Blob([hdr + rows], { type: 'text/csv' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url; a.download = 'telemetry_log.csv'; a.click();
    URL.revokeObjectURL(url);
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

// Log buttons
const btnLogInfo  = $('btn-log-info');
const btnLogClear = $('btn-log-clear');
const btnLogCsv   = $('btn-log-csv');
if (btnLogInfo)  btnLogInfo.addEventListener('click', () => wsSend({ type: 'get_log_info' }));
if (btnLogClear) btnLogClear.addEventListener('click', () => wsSend({ type: 'clear_log' }));
if (btnLogCsv)   btnLogCsv.addEventListener('click', () => {
    pendingLogFrames = [];
    pendingLogTotal = -2;
    wsSend({ type: 'get_log_info' });
});

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
