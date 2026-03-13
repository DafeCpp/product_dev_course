// WebSocket подключение
let ws = null;
let wsReconnectInterval = null;
const WS_URL = `ws://${window.location.hostname}/ws`;

// Функция для отправки JSON-команды через WebSocket
function wsSend(obj) {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify(obj));
    }
}

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
const btnCalibAutoForward = document.getElementById('btn-calib-auto-forward');
const btnCalibForward = document.getElementById('btn-calib-forward');
const calibFwdThrottleSlider = document.getElementById('calib-fwd-throttle');
const calibFwdThrottleValueEl = document.getElementById('calib-fwd-throttle-value');

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

// Калибровка IMU UI
const calibStatusEl = document.getElementById('calib-status');
const calibValidEl = document.getElementById('calib-valid');
const calibBiasEl = document.getElementById('calib-bias');
const btnCalibGyro = document.getElementById('btn-calib-gyro');
const btnCalibFull = document.getElementById('btn-calib-full');

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
                } else if (data.type === 'calibrate_imu_ack') {
                    updateCalibStatus(data.status, null);
                } else if (data.type === 'calib_status') {
                    updateCalibStatus(data.status, null);
                } else if (data.type === 'stab_config' || data.type === 'set_stab_config_ack') {
                    if (data.type === 'stab_config') {
                        applyStabConfig(data);
                    } else if (data.ok) {
                        applyStabConfig(data);
                        showStabSaveStatus('Сохранено', 'connected');
                    } else {
                        showStabSaveStatus('Ошибка сохранения', 'disconnected');
                    }
                } else if (data.type === 'log_info') {
                    updateLogInfo(data.count, data.capacity);
                    // Если ожидается CSV — запросить данные
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
                }
            } catch (e) {
                console.error('Failed to parse message:', e);
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

    let throttle = parseFloat(throttleSlider.value);
    let steering = parseFloat(steeringSlider.value);

    // Детский режим: ограничить газ и руль
    if (kidsMode) {
        throttle = Math.max(-kidsThrottleLimit, Math.min(kidsThrottleLimit, throttle));
        steering = Math.max(-kidsSteeringLimit, Math.min(kidsSteeringLimit, steering));
    }

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

// Обновление статуса калибровки IMU
function updateCalibStatus(status, calib) {
    if (calibStatusEl && status) {
        const labels = {
            idle: 'Ожидание',
            collecting: 'Сбор данных...',
            done: 'Завершена',
            failed: 'Ошибка (движение)'
        };
        calibStatusEl.textContent = labels[status] || status;
        calibStatusEl.className = 'status-value ' + (status || 'unknown');

        const collecting = (status === 'collecting');
        if (btnCalibGyro) btnCalibGyro.disabled = collecting;
        if (btnCalibFull) btnCalibFull.disabled = collecting;
        if (btnCalibForward) btnCalibForward.disabled = collecting;
        if (btnCalibAutoForward) btnCalibAutoForward.disabled = collecting;
    }

    if (calib) {
        if (calibValidEl) {
            calibValidEl.textContent = calib.valid ? 'Валидны' : 'Нет данных';
            calibValidEl.className = 'status-value ' + (calib.valid ? 'done' : 'idle');
        }

        if (calib.bias && calibBiasEl) {
            calibBiasEl.style.display = 'block';
            const set = (id, v) => {
                const el = document.getElementById(id);
                if (el) el.textContent = (typeof v === 'number') ? v.toFixed(4) : '—';
            };
            set('calib-gx', calib.bias.gx);
            set('calib-gy', calib.bias.gy);
            set('calib-gz', calib.bias.gz);
            set('calib-ax', calib.bias.ax);
            set('calib-ay', calib.bias.ay);
            set('calib-az', calib.bias.az);
        } else if (calibBiasEl && !calib.valid) {
            calibBiasEl.style.display = 'none';
        }
    }
}

// Отправка команды калибровки
function sendCalibrate(mode) {
    if (!ws || ws.readyState !== WebSocket.OPEN) return;
    ws.send(JSON.stringify({ type: 'calibrate_imu', mode: mode }));
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

    // EKF блок (Phase 4.1/4.2)
    if (data.ekf) {
        html += `<div class="telem-item">
            <span class="telem-label">EKF Slip:</span>
            <span class="telem-value">${data.ekf.slip_deg?.toFixed(1) || 'N/A'} °</span>
        </div>`;
        html += `<div class="telem-item">
            <span class="telem-label">EKF Speed:</span>
            <span class="telem-value">${data.ekf.speed_ms?.toFixed(2) || 'N/A'} m/s</span>
        </div>`;
        html += `<div class="telem-item">
            <span class="telem-label">EKF Vx/Vy:</span>
            <span class="telem-value">${data.ekf.vx?.toFixed(2) || 'N/A'} / ${data.ekf.vy?.toFixed(2) || 'N/A'} m/s</span>
        </div>`;
    }

    telemDataEl.innerHTML = html || '<p>Нет данных</p>';

    // Обновление панели калибровки из телеметрии
    if (data.calib) {
        updateCalibStatus(data.calib.status, data.calib);
    }

    // Oversteer индикатор (Phase 4.2)
    const owIndicator = document.getElementById('oversteer-indicator');
    const owStatus = document.getElementById('oversteer-status');
    if (owIndicator && owStatus && data.warn !== undefined) {
        owIndicator.style.display = 'flex';
        if (data.warn.oversteer) {
            owStatus.textContent = 'ЗАНОС!';
            owStatus.className = 'status-value oversteer-active';
        } else {
            owStatus.textContent = 'OK';
            owStatus.className = 'status-value oversteer-ok';
        }
    }
}

// ═══════════════════════════════════════════════════════════════════
// Стабилизация (Phase 4.1/4.2/4.4)
// ═══════════════════════════════════════════════════════════════════

let currentMode = 0;  // 0=normal, 1=sport, 2=drift, 3=kids, 4=direct
let kidsThrottleLimit = 0.50;  // 50%
let kidsSteeringLimit = 0.70;  // 70%
let kidsMode = false;

function applyStabConfig(cfg) {
    const set = (id, val) => {
        const el = document.getElementById(id);
        if (el) el.value = val;
    };
    const setChk = (id, val) => {
        const el = document.getElementById(id);
        if (el) el.checked = !!val;
    };

    currentMode = cfg.mode ?? 0;
    updateModeButtons(currentMode);

    setChk('stab-enabled', cfg.enabled);

    // Прошивка возвращает вложенный JSON: yaw_rate.pid.kp и т.д.
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

    // Детский режим: синхронизировать слайдеры с конфигом прошивки
    const km = cfg.kids_mode;
    if (km) {
        const tPct = Math.round((km.throttle_limit ?? 0.5) * 100);
        const sPct = Math.round((km.steering_limit ?? 0.7) * 100);
        kidsThrottleLimit = km.throttle_limit ?? 0.5;
        kidsSteeringLimit = km.steering_limit ?? 0.7;
        set('kids-throttle-limit', tPct);
        set('kids-steering-limit', sPct);
        const tEl = document.getElementById('kids-throttle-value');
        const sEl = document.getElementById('kids-steering-value');
        if (tEl) tEl.textContent = tPct;
        if (sEl) sEl.textContent = sPct;
    }
}

function updateModeButtons(mode) {
    const ids = ['btn-mode-normal', 'btn-mode-sport', 'btn-mode-drift', 'btn-mode-kids', 'btn-mode-direct'];
    ids.forEach((id, idx) => {
        const el = document.getElementById(id);
        if (!el) return;
        if (idx === mode) {
            el.classList.add('btn-mode-active');
        } else {
            el.classList.remove('btn-mode-active');
        }
    });

    // Direct Law banner
    const directBanner = document.getElementById('direct-law-banner');
    const controlPanel = document.getElementById('control-panel');

    if (mode === 4) {
        if (directBanner) directBanner.style.display = 'block';
        if (controlPanel) controlPanel.classList.add('direct-active');
    } else {
        if (directBanner) directBanner.style.display = 'none';
        if (controlPanel) controlPanel.classList.remove('direct-active');
    }

    // Kids mode settings visibility (independent of drive mode)
    updateKidsModeUI();
}

function updateKidsModeUI() {
    const kidsSettings = document.getElementById('kids-mode-settings');
    const kidsBanner = document.getElementById('kids-mode-banner');
    const controlPanel = document.getElementById('control-panel');
    const toggleBtn = document.getElementById('btn-kids-toggle');

    // Показываем настройки если включён browser-toggle ИЛИ выбран режим Kids (3)
    const kidsActive = kidsMode || currentMode === 3;
    if (kidsActive) {
        if (kidsSettings) kidsSettings.style.display = 'block';
        if (kidsBanner) kidsBanner.style.display = 'block';
        if (controlPanel) controlPanel.classList.add('kids-active');
        if (toggleBtn) toggleBtn.textContent = 'Детский режим: ВКЛ';
        if (toggleBtn) toggleBtn.classList.add('btn-kids-active');
    } else {
        if (kidsSettings) kidsSettings.style.display = 'none';
        if (kidsBanner) kidsBanner.style.display = 'none';
        if (controlPanel) controlPanel.classList.remove('kids-active');
        if (toggleBtn) toggleBtn.textContent = 'Детский режим: ВЫКЛ';
        if (toggleBtn) toggleBtn.classList.remove('btn-kids-active');
    }
}

function loadStabConfig() {
    wsSend({ type: 'get_stab_config' });
}

function saveStabConfig() {
    const getF = (id) => {
        const el = document.getElementById(id);
        return el ? parseFloat(el.value) : undefined;
    };
    const getChk = (id) => {
        const el = document.getElementById(id);
        return el ? el.checked : false;
    };

    // Прошивка ожидает вложенный JSON — StabilizationConfigFromJson
    const cfg = {
        type: 'set_stab_config',
        mode: currentMode,
        enabled: getChk('stab-enabled'),
        filter: {
            adaptive_beta_enabled: getChk('adaptive-beta-enabled'),
            adaptive_accel_threshold_g: getF('adaptive-accel-thresh'),
        },
        yaw_rate: {
            pid: {
                kp: getF('stab-kp'),
                ki: getF('stab-ki'),
                kd: getF('stab-kd'),
                max_correction: getF('stab-max-corr'),
            },
        },
        adaptive: {
            enabled: getChk('adapt-pid-enabled'),
            speed_ref_ms: getF('adapt-speed-ref'),
        },
        pitch_comp: {
            enabled: getChk('pitch-comp-enabled'),
            gain: getF('pitch-gain'),
            max_correction: getF('pitch-max-corr'),
        },
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
    };
    wsSend(cfg);
}

function showStabSaveStatus(msg, cssClass) {
    const statusEl = document.getElementById('stab-save-status');
    const msgEl = document.getElementById('stab-save-msg');
    if (!statusEl || !msgEl) return;
    statusEl.style.display = 'flex';
    msgEl.textContent = msg;
    msgEl.className = 'status-value ' + cssClass;
    setTimeout(() => { statusEl.style.display = 'none'; }, 3000);
}

// ═══════════════════════════════════════════════════════════════════
// Лог телеметрии (Phase 4.3/4.4)
// ═══════════════════════════════════════════════════════════════════

let pendingLogFrames = [];
let pendingLogTotal = 0;
let pendingLogOffset = 0;

function updateLogInfo(count, capacity) {
    const countEl = document.getElementById('log-count');
    const capEl = document.getElementById('log-capacity');
    if (countEl) countEl.textContent = count ?? '—';
    if (capEl) capEl.textContent = capacity ?? '—';
}

function getLogInfo() {
    wsSend({ type: 'get_log_info' });
}

function clearLog() {
    wsSend({ type: 'clear_log' });
}

function downloadLogCsv() {
    // Запрашиваем последние 500 кадров через get_log_data
    pendingLogFrames = [];
    // Сначала узнаём текущее количество кадров
    wsSend({ type: 'get_log_info' });
    // Загрузка будет инициирована после получения log_info
    pendingLogTotal = -1;  // сигнал что нужна выгрузка
}

function handleLogData(frames) {
    if (!Array.isArray(frames)) return;
    pendingLogFrames = pendingLogFrames.concat(frames);

    if (pendingLogTotal > 0) {
        if (pendingLogFrames.length >= pendingLogTotal) {
            // Все кадры получены
            exportLogCsv(pendingLogFrames);
            pendingLogFrames = [];
            pendingLogTotal = 0;
        } else {
            // Запросить следующую порцию
            const nextOffset = pendingLogOffset + pendingLogFrames.length;
            const remaining = pendingLogTotal - pendingLogFrames.length;
            wsSend({ type: 'get_log_data', offset: nextOffset, count: Math.min(200, remaining) });
        }
    }
}

function exportLogCsv(frames) {
    if (!frames || frames.length === 0) {
        alert('Нет данных для скачивания');
        return;
    }
    const header = 'ts_ms,vx,vy,slip_deg,speed_ms,throttle,steering\n';
    const rows = frames.map(f =>
        `${f.ts_ms},${f.vx},${f.vy},${f.slip_deg},${f.speed_ms},${f.throttle},${f.steering}`
    ).join('\n');
    const blob = new Blob([header + rows], { type: 'text/csv' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 'telemetry_log.csv';
    a.click();
    URL.revokeObjectURL(url);
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

// Кнопки калибровки IMU
if (btnCalibGyro) {
    btnCalibGyro.addEventListener('click', () => sendCalibrate('gyro'));
}
if (btnCalibFull) {
    btnCalibFull.addEventListener('click', () => sendCalibrate('full'));
}
if (btnCalibForward) {
    btnCalibForward.addEventListener('click', () => sendCalibrate('forward'));
}
if (btnCalibAutoForward) {
    btnCalibAutoForward.addEventListener('click', () => {
        const pct = calibFwdThrottleSlider ? parseInt(calibFwdThrottleSlider.value) : 25;
        const throttle = pct / 100;
        wsSend({ type: 'calibrate_imu', mode: 'auto_forward', throttle });
    });
}
if (calibFwdThrottleSlider) {
    calibFwdThrottleSlider.addEventListener('input', (e) => {
        if (calibFwdThrottleValueEl) calibFwdThrottleValueEl.textContent = e.target.value;
    });
}

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

// Кнопки стабилизации (Phase 4.4)
const btnLoadStab = document.getElementById('btn-load-stab');
const btnSaveStab = document.getElementById('btn-save-stab');
const btnModeNormal = document.getElementById('btn-mode-normal');
const btnModeSport = document.getElementById('btn-mode-sport');
const btnModeDrift = document.getElementById('btn-mode-drift');
const btnModeKids = document.getElementById('btn-mode-kids');
const btnModeDirect = document.getElementById('btn-mode-direct');
const btnKidsToggle = document.getElementById('btn-kids-toggle');
const btnLogInfo = document.getElementById('btn-log-info');
const btnLogClear = document.getElementById('btn-log-clear');
const btnLogCsv = document.getElementById('btn-log-csv');

if (btnLoadStab) btnLoadStab.addEventListener('click', loadStabConfig);
if (btnSaveStab) btnSaveStab.addEventListener('click', saveStabConfig);
const kidsThrottleSlider = document.getElementById('kids-throttle-limit');
const kidsSteeringSlider = document.getElementById('kids-steering-limit');
const kidsThrottleValueEl = document.getElementById('kids-throttle-value');
const kidsSteeringValueEl = document.getElementById('kids-steering-value');

if (btnModeNormal) btnModeNormal.addEventListener('click', () => { currentMode = 0; updateModeButtons(0); });
if (btnModeSport) btnModeSport.addEventListener('click', () => { currentMode = 1; updateModeButtons(1); });
if (btnModeDrift) btnModeDrift.addEventListener('click', () => { currentMode = 2; updateModeButtons(2); });
if (btnModeKids) btnModeKids.addEventListener('click', () => { currentMode = 3; updateModeButtons(3); });
if (btnModeDirect) btnModeDirect.addEventListener('click', () => { currentMode = 4; updateModeButtons(4); });
if (btnKidsToggle) btnKidsToggle.addEventListener('click', () => {
    kidsMode = !kidsMode;
    updateKidsModeUI();
});

if (kidsThrottleSlider) {
    kidsThrottleSlider.addEventListener('input', (e) => {
        const pct = parseInt(e.target.value);
        kidsThrottleLimit = pct / 100;
        if (kidsThrottleValueEl) kidsThrottleValueEl.textContent = pct;
    });
}
if (kidsSteeringSlider) {
    kidsSteeringSlider.addEventListener('input', (e) => {
        const pct = parseInt(e.target.value);
        kidsSteeringLimit = pct / 100;
        if (kidsSteeringValueEl) kidsSteeringValueEl.textContent = pct;
    });
}
if (btnLogInfo) btnLogInfo.addEventListener('click', getLogInfo);
if (btnLogClear) btnLogClear.addEventListener('click', clearLog);
if (btnLogCsv) btnLogCsv.addEventListener('click', () => {
    // Сначала запрашиваем количество, затем скачиваем последние 500 кадров
    pendingLogFrames = [];
    pendingLogTotal = -2;  // маркер "нужен CSV после log_info"
    wsSend({ type: 'get_log_info' });
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

