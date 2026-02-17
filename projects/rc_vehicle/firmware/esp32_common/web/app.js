// WebSocket подключение
let ws = null;
let wsReconnectInterval = null;
const WS_URL = `ws://${window.location.hostname}/ws`;

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

    telemDataEl.innerHTML = html || '<p>Нет данных</p>';

    // Обновление панели калибровки из телеметрии
    if (data.calib) {
        updateCalibStatus(data.calib.status, data.calib);
    }
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

