// ═══════════════════════════════════════════════════════════════════
// RC Vehicle — Web UI
// ═══════════════════════════════════════════════════════════════════

// ── WebSocket ──
let ws = null;
let wsReconnectTimer = null;
const WS_URL = `ws://${window.location.hostname}/ws`;

// ── HTML-escape helper for innerHTML interpolation ──
// Server-side data and JS exception messages must be escaped before being
// written via innerHTML to avoid reflected XSS in the local UI.
function escapeHtml(value) {
    return String(value ?? '')
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;');
}

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

// Магнитометр
const magCalibBadge   = $('mag-calib-badge');
const magHeadingAbs   = $('mag-heading-abs');
const magHeadingRel   = $('mag-heading-rel');
const magXyz          = $('mag-xyz');
const magCalibStatus  = $('mag-calib-status');
const magCalibMsg     = $('mag-calib-msg');
const btnMagStart     = $('btn-mag-start');
const btnMagFinish    = $('btn-mag-finish');
const btnMagCancel    = $('btn-mag-cancel');
const btnMagErase     = $('btn-mag-erase');
const btnResetHeading = $('btn-reset-heading-ref');
const magCompassCanvas= $('mag-compass');

// ── State ──
let lastCommandSeq = 0;
let commandSendInterval = null;
let lastTelemTime = 0;
let wsConnectTime = 0;
let telemRxCount = 0;
const MCU_TIMEOUT_MS = 1500;
let mcuStatusCheckInterval = null;
let wifiStatusInterval = null;
let magCalibPollTimer = null;
let magCalibRefreshTimer = null;
let lastMagEraseResult = 'none';
let magEraseWatching = false;
let magEraseWatchTimer = null;
let magEraseWatchAttempts = 0;
let magEraseTargetSeq = 0;

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
            wsSend({ type: 'get_mag_calib_status' });
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
                    // SetKidsModeActive() меняет режим, а StabilizationManager
                    // при смене режима перезагружает сохранённый профиль — все
                    // локальные копии (limiters_enabled, лимиты газа/руля)
                    // устаревают. Без перечитывания баннер мог бы утверждать,
                    // что защита включена, когда прошивка её сняла.
                    loadStabConfig();
                } else if (data.type === 'start_speed_calib_ack') {
                    updateSpeedCalibStatus(data);
                } else if (data.type === 'speed_calib_status') {
                    updateSpeedCalibStatus(data);
                } else if (data.type === 'stop_speed_calib_ack') {
                    if (speedCalibStatusEl) speedCalibStatusEl.textContent = 'Остановлено';
                    if (btnSpeedCalibStart) btnSpeedCalibStart.disabled = false;
                } else if (data.type === 'calibrate_mag_ack') {
                    updateMagCalibUI(data.status, data.fail_reason ?? 'none', data.erase_result ?? 'none');
                    // action эхом от сервера — надёжнее клиентского флага "жду
                    // ack": не путается, если erase кликнули дважды подряд или
                    // вперемешку с start/finish/cancel (ревью PR #308).
                    //
                    // erase_target_seq назначен сервером АТОМАРНО в момент
                    // приёма команды — в отличие от erase_seq (просто текущий
                    // снимок на момент ack), его не нужно достраивать на
                    // клиенте прибавлением 1: любое клиентское вычисление
                    // target из erase_seq снимка гоняется с control loop,
                    // который мог успеть применить именно этот erase между
                    // постановкой в очередь и чтением снимка (ревью PR #308).
                    if (data.action === 'erase' && data.ok) extendMagEraseWatch(data.erase_target_seq ?? 0);
                    scheduleMagCalibRefresh();
                } else if (data.type === 'mag_calib_status') {
                    updateMagCalibUI(data.status, data.fail_reason ?? 'none', data.erase_result ?? 'none');
                    onMagEraseSeqObserved(data.erase_seq ?? 0);
                } else if (data.type === 'reset_heading_ref_ack') {
                    if (magCalibMsg) { magCalibMsg.textContent = 'Нулевой курс сброшен'; magCalibMsg.style.display = 'block'; setTimeout(() => { if (magCalibMsg) magCalibMsg.style.display = 'none'; }, 2000); }
                }
            } catch (e) {
                console.error('Parse error:', e, event.data?.slice(0, 200));
                if (telemDataEl) {
                    telemDataEl.innerHTML = `<p style="color:var(--danger)">JS error: ${escapeHtml(e.message)}</p>`;
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

    // Предварительный клэмп на клиенте зеркалит прошивку: при снятом
    // мастер-выключателе ограничителей она команду не режет (LOS-286).
    if (kidsMode && kidsLimitersEnabled) {
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
// Magnetometer UI
// ═══════════════════════════════════════════════════════════════════

const MAG_FAIL_REASON_TEXT = {
    too_few_samples: 'Мало семплов — нажмите Start и вращайте машину дольше (>2 сек)',
    radius_too_small: 'Недостаточное вращение — покрутите машину по кругу во всех направлениях',
    radius_too_large: 'Сильные помехи — уберите магниты и металл рядом с датчиком',
    not_planar: 'Вращение не в одной плоскости — поворачивайте машину только вокруг вертикальной оси',
};

// Догоняющий запрос статуса после любой команды mag-калибровки.
//
// Прошивка исполняет start/finish/cancel не на месте, а на control loop
// (ревью PR #308), поэтому статус в calibrate_mag_ack — ещё ДО команды.
// Без этого запроса клик «Старт» из idle оставлял бы панель мёртвой:
// updateMagCalibUI заводит таймер поллинга, только увидев collecting, а ack
// приносит idle — сбор идёт на машине, но UI об этом никогда не узнаёт и
// Finish остаётся заблокированным. Для finish/cancel таймер уже крутится и
// сам бы догнал за секунду, но одинаковое поведение проще.
//
// 100 мс с запасом перекрывают тик цикла (2 мс).
function scheduleMagCalibRefresh() {
    if (magCalibRefreshTimer) clearTimeout(magCalibRefreshTimer);
    magCalibRefreshTimer = setTimeout(() => {
        magCalibRefreshTimer = null;
        wsSend({ type: 'get_mag_calib_status' });
    }, 100);
}

// Опрашивать, пока erase_seq не достигнет цели, присланной сервером в
// erase_target_seq. Раньше цель клиент считал сам — прибавлял 1 к уже
// известному erase_seq. Это ломается ровно тогда, когда control loop успевал
// применить только что принятый erase МЕЖДУ постановкой его в очередь на
// сервере и последующим чтением снимка состояния для ack: ack тогда уже нёс
// erase_seq ЭТОЙ команды, а клиентское "+1" целилось в следующую, которой не
// будет — наблюдение зависало бы до таймаута (ревью PR #308). Сервер
// назначает target_seq атомарно вместе с самим приёмом команды (см.
// EraseMagCalibration()), поэтому клиенту достаточно просто использовать его,
// не пересчитывая.
//
// Перекрывающиеся erase (кликнули второй, не дождавшись первого) по той же
// причине больше не требуют суммирования на клиенте: target — максимум из
// всех erase_target_seq, что видел клиент, действующая цель не уменьшается.
//
// Одноразового scheduleMagCalibRefresh (100 мс) достаточно для status/
// fail_reason: пока status==collecting, их и так продолжает подтягивать
// поллинг раз в секунду ниже. Для erase такой страховки нет — стирание не
// меняет status/fail_reason калибровки в памяти, а синхронная запись NVS
// может занять заметно больше 100 мс.
const MAG_ERASE_WATCH_INTERVAL_MS = 150;
const MAG_ERASE_WATCH_MAX_ATTEMPTS = 20;  // 20 × 150 мс = 3 с — с запасом на NVS

function extendMagEraseWatch(targetSeq) {
    magEraseTargetSeq = Math.max(magEraseTargetSeq, targetSeq);
    magEraseWatching = true;
    magEraseWatchAttempts = 0;  // каждый принятый erase заслуживает свой полный бюджет попыток
    scheduleMagEraseWatchPoll();
}

function scheduleMagEraseWatchPoll() {
    if (magEraseWatchTimer) clearTimeout(magEraseWatchTimer);
    magEraseWatchTimer = setTimeout(() => {
        magEraseWatchTimer = null;
        wsSend({ type: 'get_mag_calib_status' });
    }, MAG_ERASE_WATCH_INTERVAL_MS);
}

function onMagEraseSeqObserved(eraseSeq) {
    if (!magEraseWatching) return;
    magEraseWatchAttempts++;
    if (eraseSeq >= magEraseTargetSeq) {
        magEraseWatching = false;  // все принятые на этот момент erase применились
        return;
    }
    if (magEraseWatchAttempts >= MAG_ERASE_WATCH_MAX_ATTEMPTS) {
        magEraseWatching = false;  // не дождались — сообщаем об этом честно
        if (magCalibMsg) {
            magCalibMsg.textContent = 'Не удалось подтвердить результат стирания калибровки';
            magCalibMsg.style.display = 'block';
        }
        return;
    }
    scheduleMagEraseWatchPoll();
}

function updateMagCalibUI(status, failReason, eraseResult) {
    lastMagEraseResult = eraseResult;
    const collecting = status === 'collecting';
    const done       = status === 'done';
    const failed     = status === 'failed';

    // Кнопки
    if (btnMagStart)  btnMagStart.disabled  = collecting;
    if (btnMagFinish) btnMagFinish.disabled = !collecting;
    if (btnMagCancel) btnMagCancel.disabled = !collecting;

    // Бейдж в заголовке панели
    const badgeClass = done ? 'badge-ok' : failed ? 'badge-err' : collecting ? 'badge-warn' : 'badge-unknown';
    const badgeText  = { idle: '—', collecting: 'Сбор', done: 'OK', failed: 'Ошибка' }[status] ?? '—';
    if (magCalibBadge) { magCalibBadge.className = `badge ${badgeClass}`; magCalibBadge.textContent = badgeText; }

    // Статус-строка с причиной ошибки
    let statusText = { idle: 'Ожидание', collecting: 'Сбор данных...', done: 'Калибровка OK', failed: 'Ошибка' }[status] ?? status;
    if (failed && failReason && failReason !== 'none') {
        statusText = MAG_FAIL_REASON_TEXT[failReason] ?? `Ошибка: ${failReason}`;
    }
    if (magCalibStatus) magCalibStatus.textContent = statusText;

    // Сообщение: результат erase — единственный сигнал о нём, т.к. само
    // стирание NVS не меняет status/fail_reason калибровки в памяти
    // (ревью PR #308). Держится, пока не придёт следующий erase с другим
    // исходом — это последний известный результат, а не разовое уведомление.
    if (magCalibMsg) {
        if (eraseResult === 'failed') {
            magCalibMsg.textContent = 'Стирание калибровки в NVS не удалось';
            magCalibMsg.style.display = 'block';
        } else {
            magCalibMsg.style.display = 'none';
        }
    }

    // Polling пока collecting
    if (collecting && !magCalibPollTimer) {
        magCalibPollTimer = setInterval(() => wsSend({ type: 'get_mag_calib_status' }), 1000);
    } else if (!collecting && magCalibPollTimer) {
        clearInterval(magCalibPollTimer);
        magCalibPollTimer = null;
    }
}

function drawCompass(headingDeg) {
    if (!magCompassCanvas) return;
    const ctx = magCompassCanvas.getContext('2d');
    const w = magCompassCanvas.width, h = magCompassCanvas.height;
    const cx = w / 2, cy = h / 2, r = Math.min(cx, cy) - 4;
    ctx.clearRect(0, 0, w, h);

    // Круг
    ctx.beginPath();
    ctx.arc(cx, cy, r, 0, Math.PI * 2);
    ctx.strokeStyle = '#555';
    ctx.lineWidth = 1.5;
    ctx.stroke();

    // Стрелка (красная — направление относительного нуля)
    const rad = (headingDeg - 90) * Math.PI / 180;
    ctx.beginPath();
    ctx.moveTo(cx + Math.cos(rad) * r * 0.75, cy + Math.sin(rad) * r * 0.75);
    ctx.lineTo(cx - Math.cos(rad) * r * 0.35, cy - Math.sin(rad) * r * 0.35);
    ctx.strokeStyle = '#e05';
    ctx.lineWidth = 2.5;
    ctx.stroke();

    // Метка N
    ctx.fillStyle = '#888';
    ctx.font = '10px sans-serif';
    ctx.textAlign = 'center';
    ctx.fillText('N', cx, cy - r + 12);
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
        html += row('EKF ZUPT', data.ekf.zupt_status ?? 'N/A');
    }
    if (data.imu?.orientation) {
        const o = data.imu.orientation;
        html += row('Pitch', (o.pitch?.toFixed(1) ?? 'N/A') + ' °');
        html += row('Roll', (o.roll?.toFixed(1) ?? 'N/A') + ' °');
        html += row('Yaw', (o.yaw?.toFixed(1) ?? 'N/A') + ' °');
    }
    if (data.mag) {
        html += row('Heading (abs)', (data.mag.heading_deg?.toFixed(1) ?? 'N/A') + ' °');
        html += row('Heading (rel)', (data.mag.heading_rel_deg?.toFixed(1) ?? 'N/A') + ' °');
        html += row('Mx/My/Mz', `${data.mag.mx?.toFixed(0) ?? '?'} / ${data.mag.my?.toFixed(0) ?? '?'} / ${data.mag.mz?.toFixed(0) ?? '?'} mG`);
        // Обновить панель компаса
        if (magHeadingAbs) magHeadingAbs.textContent = (data.mag.heading_deg?.toFixed(1) ?? '—') + ' °';
        if (magHeadingRel) magHeadingRel.textContent = (data.mag.heading_rel_deg?.toFixed(1) ?? '—') + ' °';
        if (magXyz) magXyz.textContent = `${data.mag.mx?.toFixed(0) ?? '?'} / ${data.mag.my?.toFixed(0) ?? '?'} / ${data.mag.mz?.toFixed(0) ?? '?'} mG`;
        if (data.mag.heading_rel_deg != null) drawCompass(data.mag.heading_rel_deg);
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
// Мастер-выключатель ограничителей Kids (LOS-286). Не связан с stab-enabled:
// тот управляет только контурами стабилизации.
let kidsLimitersEnabled = true;

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
        kidsLimitersEnabled = km.limiters_enabled ?? true;
        setChk('kids-limiters-enabled', kidsLimitersEnabled);

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

        const kidsSlewSteering = km.slew_steering ?? 3.0;
        const kidsSlewThrottle = km.slew_throttle ?? 0.3;
        set('kids-slew-steering', kidsSlewSteering);
        set('kids-slew-throttle', kidsSlewThrottle);
        const kidsSteerEl = $('kids-slew-steering-value');
        const kidsThrottleEl = $('kids-slew-throttle-value');
        if (kidsSteerEl) kidsSteerEl.textContent = kidsSlewSteering.toFixed(1);
        if (kidsThrottleEl) kidsThrottleEl.textContent = kidsSlewThrottle.toFixed(1);
        updateKidsEffectiveSlew();

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
    updateKidsModeUI();
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
    if (kidsBanner) {
        kidsBanner.style.display = active ? 'block' : 'none';
        kidsBanner.textContent = kidsLimitersEnabled
            ? 'Детский режим активен'
            : 'Детский режим: ОГРАНИЧИТЕЛИ СНЯТЫ';
        kidsBanner.classList.toggle('banner-danger', !kidsLimitersEnabled);
        kidsBanner.classList.toggle('banner-warn', kidsLimitersEnabled);
    }
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
            limiters_enabled: getChk('kids-limiters-enabled'),
            throttle_limit: kidsThrottleLimit,
            reverse_limit: kidsThrottleLimit,
            steering_limit: kidsSteeringLimit,
            slew_steering: getF('kids-slew-steering'),
            slew_throttle: getF('kids-slew-throttle'),
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
    const hdr = 'ts_ms,ax,ay,az,gx,gy,gz,vx,vy,slip_deg,speed_ms,throttle,steering,pitch_deg,roll_deg,yaw_deg,yaw_rate_dps,oversteer_active,rc_throttle,rc_steering,cmd_throttle,cmd_steering,ekf_vx_var,ekf_vy_var,ekf_r_var,ekf_yaw_deg,mx,my,mz,heading_deg,heading_rel_deg,test_marker,zupt_status,ekf_diverged,drive_mode,stab_enabled\n';
    const rows = frames.map(f =>
        `${f.ts_ms},${f.ax},${f.ay},${f.az},${f.gx},${f.gy},${f.gz},${f.vx},${f.vy},${f.slip_deg},${f.speed_ms},${f.throttle},${f.steering},${f.pitch_deg},${f.roll_deg},${f.yaw_deg},${f.yaw_rate_dps},${f.oversteer_active},${f.rc_throttle},${f.rc_steering},${f.cmd_throttle??0},${f.cmd_steering??0},${f.ekf_vx_var??0},${f.ekf_vy_var??0},${f.ekf_r_var??0},${f.ekf_yaw_deg??0},${f.mx??0},${f.my??0},${f.mz??0},${f.heading_deg??0},${f.heading_rel_deg??0},${f.test_marker??0},${f.zupt_status??0},${f.ekf_diverged??0},${f.drive_mode??0},${f.stab_enabled??0}`
    ).join('\n');
    triggerDownload(hdr + rows, 'telemetry_log.csv', 'text/csv');
}

// ─────────────────────────────────────────────────────────────
// Binary log download via HTTP
// Format: see http_server.cpp log_bin_handler for full protocol description.
//   Section 1: [4B frame_count][4B frame_size][frame_count × frame_size bytes]
//   Section 2: [4B event_count][4B event_size][event_count × event_size bytes]
// ─────────────────────────────────────────────────────────────

// TelemetryEventType names (must match telemetry_event_log.hpp)
const EVENT_TYPE_NAMES = {
    1:  'ImuCalibStart',     2:  'ImuCalibDone',      3:  'ImuCalibFailed',
    4:  'TrimCalibStart',    5:  'TrimCalibDone',      6:  'TrimCalibFailed',
    7:  'ComCalibStart',     8:  'ComCalibDone',       9:  'ComCalibFailed',
    10: 'SpeedCalibStart',   11: 'SpeedCalibDone',     12: 'SpeedCalibFailed',
    13: 'TestStart',         14: 'TestDone',           15: 'TestFailed',
    16: 'TestStopped',
    17: 'MagCalibStart',     18: 'MagCalibDone',       19: 'MagCalibFailed',
    20: 'MagCalibCancelled',
};

function eventParamDesc(typeId, param) {
    // Test events: param = TestType
    if (typeId >= 13 && typeId <= 16) {
        return ['', 'Straight', 'Circle', 'Step'][param] || String(param);
    }
    // ImuCalibStart: param = mode
    if (typeId === 1) {
        return ['gyro_only', 'full', 'auto_forward'][param] || String(param);
    }
    // ImuCalibDone/Failed: param = stage number
    if (typeId === 2 || typeId === 3) {
        return param ? 'stage' + param : '';
    }
    return param ? String(param) : '';
}

function parseConfigSnapshot(view, base, size) {
    const n = view.getUint16(base + 6, true);
    if (view.getUint16(base + 4, true) !== 1 || n !== 63 || size < 8 + n * 4) return null;
    const v = Array.from({length: n}, (_, i) => view.getFloat32(base + 8 + i * 4, true));
    const b = i => v[i] !== 0;
    const pid = i => ({kp: v[i], ki: v[i + 1], kd: v[i + 2], max_integral: v[i + 3], max_correction: v[i + 4]});
    return {enabled: b(0), mode: v[1], fade_ms: v[2],
        filter: {madgwick_beta: v[3], lpf_cutoff_hz: v[4], imu_sample_rate_hz: v[5], madgwick_enabled: b(6), ekf_enabled: b(7), adaptive_beta_enabled: b(8), adaptive_accel_threshold_g: v[9], motor_model_enabled: b(10), motor_speed_gain: v[11], motor_deadzone: v[12], speed_meas_noise: v[13], nhc_enabled: b(14), nhc_noise: v[15], tilt_comp_enabled: b(16), tilt_corr_gain_hz: v[17], tilt_accel_gate_band_g: v[18]},
        yaw_rate: {pid: pid(19), steer_to_yaw_rate_dps: v[24]}, slip_angle: {pid: pid(25), target_deg: v[30]},
        adaptive: {enabled: b(31), speed_ref_ms: v[32], scale_min: v[33], scale_max: v[34]},
        oversteer: {warn_enabled: b(35), slip_thresh_deg: v[36], rate_thresh_deg_s: v[37], throttle_reduction: v[38]},
        pitch_comp: {enabled: b(39), gain: v[40], max_correction: v[41]},
        kids_mode: {throttle_limit: v[42], reverse_limit: v[43], steering_limit: v[44], slew_throttle: v[45], slew_steering: v[46], anti_spin_enabled: b(47), anti_spin_threshold_deg: v[48], anti_spin_reduction: v[49], accel_limit_enabled: b(50), accel_threshold_g: v[51], accel_limit_gain: v[52], accel_max_reduction: v[53], speed_limit_enabled: b(54), max_speed_ms: v[55], speed_limit_gain: v[56]},
        slew_throttle: v[57], slew_steering: v[58], steering_trim: v[59], throttle_trim: v[60], braking_mode: v[61], brake_slew_multiplier: v[62]};
}

function csvCell(value) {
    const text = String(value ?? '');
    return /[",\n\r]/.test(text) ? '"' + text.replace(/"/g, '""') + '"' : text;
}

function mergeFrameEvent(existing, added) {
    if (!existing) return added;
    return {
        name: existing.name + '|' + added.name,
        desc: existing.desc + '|' + added.desc,
        v1: existing.v1 + '|' + added.v1,
        v2: existing.v2 + '|' + added.v2,
        configs: (existing.configs || []).concat(added.configs || []),
    };
}

// Timestamp values are uint32_t milliseconds. All compared points belong to
// one retained telemetry window (far shorter than 2^31 ms), so the signed
// delta preserves their chronological ordering across uint32 rollover.
function timestampDelta(lhs, rhs) {
    return (lhs - rhs) | 0;
}

function timestampAtOrBefore(lhs, rhs) {
    return timestampDelta(lhs, rhs) <= 0;
}

// Unlike config snapshots, sparse events are not guaranteed to share a
// signed-delta horizon with the frame ring. Keep only timestamps that fit the
// concrete retained frame window, including its small lead/tail allowances.
function timestampInFrameWindow(ts, firstTs, lastTs, leadMs, tailMs) {
    const forwardFromFirst = (ts - firstTs) >>> 0;
    const backwardFromFirst = (firstTs - ts) >>> 0;
    const frameSpan = (lastTs - firstTs) >>> 0;
    return backwardFromFirst <= leadMs ||
           forwardFromFirst <= frameSpan + tailMs;
}

/**
 * Разложить события по кадрам телеметрии.
 *
 * Кадр пишется раз в kLogIntervalMs (10 мс), а события ставят метку времени
 * тика (2 мс), поэтому точное совпадение меток случается примерно в одном
 * случае из пяти — раньше остальные события просто терялись при экспорте
 * (LOS-226). Каждому кадру достаются события из интервала
 * (метка предыдущего кадра, метка текущего].
 *
 * События сильно старше первого кадра отбрасываются: кольца кадров и
 * событий переполняются независимо, поэтому в буфере легко оказываются
 * события прошлых прогонов. Прижимать их к нулевому кадру нельзя: свежий
 * CSV начинался бы с чужого TestStart. Небольшой зазор перед первым кадром
 * при этом сохраняется — сразу после «Очистить лог» событие старта успевает
 * записаться раньше первого кадра, и оно законное.
 *
 * События позже последнего кадра прижимаются к нему: кадры пишутся
 * непрерывно, так что отставание тут в пределах одного интервала.
 *
 * @param {Array<{ts:number,name:string,desc:string,v1:string,v2:string}>} events
 * @param {Array<number>} frameTs метки кадров, по возрастанию
 * @returns {Map<number, {name:string,desc:string,v1:string,v2:string}>}
 *          индекс кадра → склеенное событие ('|' между несколькими)
 */
function assignEventsToFrames(events, frameTs) {
    const byFrame = new Map();
    if (frameTs.length === 0 || events.length === 0) return byFrame;

    // Допуск на события, легшие чуть раньше первого кадра. Такое законно
    // сразу после «Очистить лог»: событие старта может успеть записаться
    // до того, как будет записан первый кадр (кадры пишутся раз в
    // kLogIntervalMs). Берём не константу, а несколько реальных шагов
    // сетки — чтобы допуск сам подстроился, если интервал логирования
    // изменится. Всё, что старше, — из прошлых прогонов (кольца кадров и
    // событий переполняются независимо).
    const gaps = [];
    for (let i = 1; i < frameTs.length; i++) {
        gaps.push((frameTs[i] - frameTs[i - 1]) >>> 0);
    }
    gaps.sort((a, b) => a - b);
    const medianGap = gaps.length ? gaps[Math.floor(gaps.length / 2)] : 10;
    const leadTolerance = 3 * medianGap;
    const tailTolerance = 3 * medianGap;
    const firstFrameTs = frameTs[0];
    const lastFrameTs = frameTs[frameTs.length - 1];

    const sorted = events
        .filter(ev => timestampInFrameWindow(
            ev.ts, firstFrameTs, lastFrameTs, leadTolerance, tailTolerance))
        .sort((a, b) => timestampDelta(a.ts, b.ts));
    let frameIdx = 0;

    for (const ev of sorted) {
        // Первый кадр с меткой >= метки события; иначе — последний кадр
        while (frameIdx < frameTs.length - 1 &&
               timestampDelta(frameTs[frameIdx], ev.ts) < 0) {
            frameIdx++;
        }
        const target = timestampDelta(frameTs[frameIdx], ev.ts) < 0
            ? frameTs.length - 1 : frameIdx;

        const prev = byFrame.get(target);
        if (prev) {
            byFrame.set(target, {
                name: prev.name + '|' + ev.name,
                desc: prev.desc + '|' + ev.desc,
                v1:   prev.v1   + '|' + ev.v1,
                v2:   prev.v2   + '|' + ev.v2,
                configs: (prev.configs || []).concat(ev.configs || []),
            });
        } else {
            byFrame.set(target, { name: ev.name, desc: ev.desc,
                                  v1: ev.v1, v2: ev.v2, configs: ev.configs || [] });
        }
    }
    return byFrame;
}

function triggerDownload(content, filename, mime) {
    const blob = new Blob([content], { type: mime });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = filename;
    a.style.display = 'none';
    document.body.appendChild(a);
    a.click();

    // На мобильных браузерах фактическое чтение blob может начаться только
    // после подтверждения системного диалога "Скачать файл". Если отозвать
    // object URL сразу после click(), загрузка иногда падает с "не удалось
    // скачать файл". Даём браузеру запас времени и только потом чистим ресурс.
    window.setTimeout(() => {
        URL.revokeObjectURL(url);
        a.remove();
    }, 60000);
}

async function downloadBinaryLog() {
    const btn = $('btn-log-csv');
    if (btn) { btn.disabled = true; btn.textContent = 'Скачивание...'; }
    try {
        const resp = await fetch('/api/log.bin', { cache: 'no-store' });
        if (!resp.ok) { alert('Ошибка: ' + resp.status); return; }
        const buf = await resp.arrayBuffer();
        const view = new DataView(buf);
        if (buf.byteLength < 8) { alert('Нет данных'); return; }

        // ── Section 1: frames ──────────────────────────────────────────────
        const frameCount = view.getUint32(0, true);
        const frameSize  = view.getUint32(4, true);
        const framesEnd  = 8 + frameCount * frameSize;

        if (frameCount === 0) { alert('Нет данных телеметрии'); return; }

        const FIELD_OFFSETS = [
            { name: 'ts_ms',           off: 0,   type: 'u32' },
            { name: 'ax',              off: 4,   type: 'f32' },
            { name: 'ay',              off: 8,   type: 'f32' },
            { name: 'az',              off: 12,  type: 'f32' },
            { name: 'gx',              off: 16,  type: 'f32' },
            { name: 'gy',              off: 20,  type: 'f32' },
            { name: 'gz',              off: 24,  type: 'f32' },
            { name: 'vx',              off: 28,  type: 'f32' },
            { name: 'vy',              off: 32,  type: 'f32' },
            { name: 'slip_deg',        off: 36,  type: 'f32' },
            { name: 'speed_ms',        off: 40,  type: 'f32' },
            { name: 'throttle',        off: 44,  type: 'f32' },
            { name: 'steering',        off: 48,  type: 'f32' },
            { name: 'pitch_deg',       off: 52,  type: 'f32' },
            { name: 'roll_deg',        off: 56,  type: 'f32' },
            { name: 'yaw_deg',         off: 60,  type: 'f32' },
            { name: 'yaw_rate_dps',    off: 64,  type: 'f32' },
            { name: 'oversteer_active',off: 68,  type: 'f32' },
            { name: 'rc_throttle',     off: 72,  type: 'f32' },
            { name: 'rc_steering',     off: 76,  type: 'f32' },
            { name: 'cmd_throttle',    off: 80,  type: 'f32' },
            { name: 'cmd_steering',    off: 84,  type: 'f32' },
            { name: 'ekf_vx_var',      off: 88,  type: 'f32' },
            { name: 'ekf_vy_var',      off: 92,  type: 'f32' },
            { name: 'ekf_r_var',       off: 96,  type: 'f32' },
            { name: 'ekf_yaw_deg',     off: 100, type: 'f32' },
            { name: 'mx',             off: 104, type: 'f32' },
            { name: 'my',             off: 108, type: 'f32' },
            { name: 'mz',             off: 112, type: 'f32' },
            { name: 'heading_deg',    off: 116, type: 'f32' },
            { name: 'heading_rel_deg',off: 120, type: 'f32' },
            { name: 'test_marker',    off: 124, type: 'u8'  },
            { name: 'zupt_status',    off: 125, type: 'u8'  },
            { name: 'ekf_diverged',   off: 126, type: 'u8'  },
            { name: 'drive_mode',     off: 127, type: 'u8'  },
            { name: 'stab_enabled',   off: 128, type: 'u8'  },
        ];

        const maxFieldEnd = FIELD_OFFSETS.reduce((maxEnd, f) => {
            const size = (f.type === 'u32' || f.type === 'f32') ? 4 : 1;
            return Math.max(maxEnd, f.off + size);
        }, 0);
        if (frameSize < maxFieldEnd) {
            alert('Неподдерживаемый размер кадра: ' + frameSize);
            return;
        }
        if (framesEnd > buf.byteLength) {
            alert('Повреждённый лог: ожидалось ' + framesEnd + ' байт, получено ' + buf.byteLength);
            return;
        }

        // ── Section 2: parse events ───────────────────────────────────────
        // Метки событий (тик, 2 мс) почти никогда не совпадают с метками
        // кадров (10 мс), поэтому раскладываем их по кадрам интервалом —
        // см. assignEventsToFrames() (LOS-226).
        const events = [];
        const snapshots = [];
        let eventCount = 0;
        let eventSize = 0;
        if (framesEnd + 8 <= buf.byteLength) {
            eventCount = view.getUint32(framesEnd,     true);
            eventSize  = view.getUint32(framesEnd + 4, true);
            if (eventSize < 8) {
                console.warn('Ignoring telemetry events with invalid size: ' + eventSize);
            } else {
                for (let i = 0; i < eventCount; i++) {
                    const base = framesEnd + 8 + i * eventSize;
                    if (base + eventSize > buf.byteLength) break;
                    const ts     = view.getUint32(base,     true);
                    const typeId = view.getUint8 (base + 4);
                    const param  = view.getUint8 (base + 5);
                    // value1/value2 at bytes 8-15 (present if eventSize >= 16)
                    const value1 = eventSize >= 16 ? view.getFloat32(base + 8,  true) : NaN;
                    const value2 = eventSize >= 16 ? view.getFloat32(base + 12, true) : NaN;
                    const name   = EVENT_TYPE_NAMES[typeId] || 'Unknown_' + typeId;
                    const desc   = eventParamDesc(typeId, param);
                    const v1str  = isNaN(value1) || value1 === 0 ? '' : value1.toFixed(4);
                    const v2str  = isNaN(value2) || value2 === 0 ? '' : value2.toFixed(4);
                    events.push({ ts, name, desc, v1: v1str, v2: v2str });
                }
            }
        }

        const eventsEnd = framesEnd + 8 + eventCount * eventSize;
        if (eventSize >= 8 && eventsEnd + 8 <= buf.byteLength) {
            const count = view.getUint32(eventsEnd, true);
            const size = view.getUint32(eventsEnd + 4, true);
            for (let i = 0; i < count; i++) {
                const base = eventsEnd + 8 + i * size;
                if (base + size > buf.byteLength) break;
                const config = parseConfigSnapshot(view, base, size);
                if (config) snapshots.push({ts: view.getUint32(base, true),
                    frameIndex: size >= 264 ? view.getUint32(base + 260, true) : null,
                    name: 'StabilizationConfigSnapshot', desc: 'schema1',
                    v1: '', v2: '', configs: [config]});
            }
        }

        // ── Build single combined CSV ──────────────────────────────────────
        const header = FIELD_OFFSETS.map(f => f.name).join(',') +
                       ',event_type,event_param,event_value1,event_value2,event_config';
        // Первый проход: разобрать кадры (события раскладываются по ним ниже,
        // поэтому нужны все метки сразу)
        const frames = [];
        for (let i = 0; i < frameCount; i++) {
            const base = 8 + i * frameSize;
            if (base + frameSize > framesEnd) break;
            frames.push(FIELD_OFFSETS.map(f => {
                const o = base + f.off;
                if (f.type === 'u32') return view.getUint32(o, true);
                if (f.type === 'u8')  return view.getUint8(o);
                return view.getFloat32(o, true);
            }));
        }

        // The binary export always starts the snapshot section with its
        // baseline record. Its timestamp may predate the retained frames by
        // more than the signed uint32 horizon, so do not infer it from time.
        const baseline = snapshots.length ? snapshots[0] : null;
        const orderedSnapshots = snapshots.slice(1)
            .filter(snapshot => snapshot.frameIndex !== null);
        events.push(...snapshots.slice(1)
            .filter(snapshot => snapshot.frameIndex === null));

        const eventByFrame = assignEventsToFrames(
            events, frames.map(vals => vals[0]));  // ts_ms — первое поле
        if (baseline) {
            eventByFrame.set(0, mergeFrameEvent(eventByFrame.get(0), baseline));
        }
        for (const snapshot of orderedSnapshots) {
            if (snapshot.frameIndex >= frames.length) continue;
            eventByFrame.set(snapshot.frameIndex,
                mergeFrameEvent(eventByFrame.get(snapshot.frameIndex), snapshot));
        }

        const frameLines = frames.map((vals, i) => {
            const ev = eventByFrame.get(i);
            return vals.concat([ev ? ev.name : '', ev ? ev.desc : '',
                                ev ? ev.v1 : '', ev ? ev.v2 : '',
                                ev && ev.configs.length ? JSON.stringify(ev.configs) : ''])
                       .map(csvCell).join(',');
        });
        const csv = header + '\n' + frameLines.join('\n');
        triggerDownload(csv, 'telemetry_log.csv', 'text/csv');

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
            row.innerHTML = `<td>${escapeHtml(t.name)}</td><td class="${t.passed ? 'st-pass' : 'st-fail'}">${t.passed ? 'PASS' : 'FAIL'}</td><td>${escapeHtml(t.value || '')}</td>`;
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

// ── Magnetometer buttons ──
if (btnMagStart)  btnMagStart.addEventListener('click',  () => wsSend({ type: 'calibrate_mag', action: 'start' }));
if (btnMagFinish) btnMagFinish.addEventListener('click', () => wsSend({ type: 'calibrate_mag', action: 'finish' }));
if (btnMagCancel) btnMagCancel.addEventListener('click', () => wsSend({ type: 'calibrate_mag', action: 'cancel' }));
if (btnMagErase)  btnMagErase.addEventListener('click',  () => {
    if (!confirm('Стереть калибровку магнитометра?')) return;
    wsSend({ type: 'calibrate_mag', action: 'erase' });
});
if (btnResetHeading) btnResetHeading.addEventListener('click', () => wsSend({ type: 'reset_heading_ref' }));

// ── Speed Calibration ──
const btnSpeedCalibStart  = $('btn-speed-calib-start');
const btnSpeedCalibStop   = $('btn-speed-calib-stop');
const speedCalibStatusEl  = $('speed-calib-status');
const speedCalibThrSlider = $('speed-calib-thr');
const speedCalibThrVal    = $('speed-calib-thr-value');
const speedCalibDurSlider = $('speed-calib-dur');
const speedCalibDurVal    = $('speed-calib-dur-value');
const speedCalibBadge     = $('speed-calib-badge');

function updateSpeedCalibStatus(data) {
    if (!speedCalibStatusEl) return;
    speedCalibStatusEl.style.display = 'block';
    if (data.status === 'started' || data.active) {
        speedCalibStatusEl.textContent = 'Калибровка скорости идёт...';
        if (btnSpeedCalibStart) btnSpeedCalibStart.disabled = true;
        if (speedCalibBadge) { speedCalibBadge.textContent = 'RUN'; speedCalibBadge.className = 'badge badge-warn'; }
        if (data.active) setTimeout(() => wsSend({ type: 'get_speed_calib_status' }), 1000);
    } else if (data.result && data.result.valid) {
        const r = data.result;
        speedCalibStatusEl.textContent =
            `Gain: ${r.speed_gain.toFixed(3)} m/s/thr | ` +
            `скорость: ${r.mean_speed_ms.toFixed(2)} m/s @ газ ${r.target_throttle.toFixed(2)} | ` +
            `${r.samples} сэмплов`;
        if (btnSpeedCalibStart) btnSpeedCalibStart.disabled = false;
        if (speedCalibBadge) { speedCalibBadge.textContent = 'DONE'; speedCalibBadge.className = 'badge badge-on'; }
    } else if (data.result && !data.result.valid) {
        speedCalibStatusEl.textContent = `Калибровка не удалась (${data.result.samples} сэмплов)`;
        if (btnSpeedCalibStart) btnSpeedCalibStart.disabled = false;
        if (speedCalibBadge) { speedCalibBadge.textContent = 'FAIL'; speedCalibBadge.className = 'badge badge-off'; }
    } else if (data.ok === false) {
        speedCalibStatusEl.textContent = data.error || 'Ошибка запуска';
        if (btnSpeedCalibStart) btnSpeedCalibStart.disabled = false;
        if (speedCalibBadge) { speedCalibBadge.textContent = 'ERR'; speedCalibBadge.className = 'badge badge-off'; }
    }
}

if (speedCalibThrSlider) speedCalibThrSlider.addEventListener('input', (e) => {
    if (speedCalibThrVal) speedCalibThrVal.textContent = parseFloat(e.target.value).toFixed(2);
});
if (speedCalibDurSlider) speedCalibDurSlider.addEventListener('input', (e) => {
    if (speedCalibDurVal) speedCalibDurVal.textContent = parseFloat(e.target.value).toFixed(1);
});

if (btnSpeedCalibStart) btnSpeedCalibStart.addEventListener('click', () => {
    const throttle = speedCalibThrSlider ? parseFloat(speedCalibThrSlider.value) : 0.3;
    const duration = speedCalibDurSlider ? parseFloat(speedCalibDurSlider.value) : 3.0;
    wsSend({ type: 'start_speed_calib', throttle, duration });
    if (speedCalibStatusEl) { speedCalibStatusEl.style.display = 'block'; speedCalibStatusEl.textContent = 'Запуск...'; }
    setTimeout(() => wsSend({ type: 'get_speed_calib_status' }), 2000);
});

if (btnSpeedCalibStop) btnSpeedCalibStop.addEventListener('click', () => {
    wsSend({ type: 'stop_speed_calib' });
    if (speedCalibStatusEl) { speedCalibStatusEl.textContent = 'Остановлено'; }
    if (btnSpeedCalibStart) btnSpeedCalibStart.disabled = false;
    if (speedCalibBadge) { speedCalibBadge.textContent = 'STOP'; speedCalibBadge.className = 'badge badge-off'; }
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

const kidsLimitersChkEl = $('kids-limiters-enabled');
if (kidsLimitersChkEl) kidsLimitersChkEl.addEventListener('change', (e) => {
    kidsLimitersEnabled = e.target.checked;
    updateKidsModeUI();
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

function updateKidsEffectiveSlew() {
    const kidsSteer = parseFloat($('kids-slew-steering')?.value);
    const kidsThrottle = parseFloat($('kids-slew-throttle')?.value);
    const globalSteer = parseFloat($('slew-steering')?.value);
    const globalThrottle = parseFloat($('slew-throttle')?.value);
    const effectiveSteer = Math.min(kidsSteer, globalSteer);
    const effectiveThrottle = Math.min(kidsThrottle, globalThrottle);
    const el = $('kids-effective-slew');
    if (el && Number.isFinite(effectiveSteer) && Number.isFinite(effectiveThrottle)) {
        el.textContent = `Эффективно: руль ${effectiveSteer.toFixed(1)}/с, газ ${effectiveThrottle.toFixed(1)}/с`;
    }
}

const kidsSlewSteeringSliderEl = $('kids-slew-steering');
const kidsSlewThrottleSliderEl = $('kids-slew-throttle');
if (kidsSlewSteeringSliderEl) kidsSlewSteeringSliderEl.addEventListener('input', (e) => {
    const v = parseFloat(e.target.value);
    const el = $('kids-slew-steering-value');
    if (el) el.textContent = v.toFixed(1);
    updateKidsEffectiveSlew();
});
if (kidsSlewThrottleSliderEl) kidsSlewThrottleSliderEl.addEventListener('input', (e) => {
    const v = parseFloat(e.target.value);
    const el = $('kids-slew-throttle-value');
    if (el) el.textContent = v.toFixed(1);
    updateKidsEffectiveSlew();
});

// Slew rate sliders
const slewSteeringSliderEl = $('slew-steering');
const slewThrottleSliderEl = $('slew-throttle');
if (slewSteeringSliderEl) slewSteeringSliderEl.addEventListener('input', (e) => {
    const v = parseFloat(e.target.value);
    const el = $('slew-steering-value');
    if (el) el.textContent = v.toFixed(1);
    updateKidsEffectiveSlew();
});
if (slewThrottleSliderEl) slewThrottleSliderEl.addEventListener('input', (e) => {
    const v = parseFloat(e.target.value);
    const el = $('slew-throttle-value');
    if (el) el.textContent = v.toFixed(1);
    updateKidsEffectiveSlew();
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
