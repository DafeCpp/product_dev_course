// WebSocket подключение
let ws = null;
let wsReconnectInterval = null;
const WS_URL = `ws://${window.location.hostname}:81/ws`;

// Элементы UI
const wsStatusEl = document.getElementById('ws-status');
const rp2040StatusEl = document.getElementById('rp2040-status');
const throttleSlider = document.getElementById('throttle');
const steeringSlider = document.getElementById('steering');
const throttleValueEl = document.getElementById('throttle-value');
const steeringValueEl = document.getElementById('steering-value');
const btnCenter = document.getElementById('btn-center');
const btnStop = document.getElementById('btn-stop');
const telemDataEl = document.getElementById('telem-data');

// Состояние
let lastCommandSeq = 0;
let commandSendInterval = null;

// Подключение к WebSocket
function connectWebSocket() {
    try {
        ws = new WebSocket(WS_URL);

        ws.onopen = () => {
            console.log('WebSocket connected');
            wsStatusEl.textContent = 'Connected';
            wsStatusEl.className = 'status-value connected';
            clearInterval(wsReconnectInterval);

            // Запуск отправки команд
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
            wsStatusEl.textContent = 'Disconnected';
            wsStatusEl.className = 'status-value disconnected';
            stopCommandSending();

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

// Обновление телеметрии
function updateTelem(data) {
    if (data.link) {
        if (data.link.rc_ok || data.link.wifi_ok) {
            rp2040StatusEl.textContent = 'OK';
            rp2040StatusEl.className = 'status-value connected';
        } else {
            rp2040StatusEl.textContent = 'Error';
            rp2040StatusEl.className = 'status-value disconnected';
        }
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

// Инициализация при загрузке страницы
window.addEventListener('load', () => {
    connectWebSocket();
});

// Отключение при закрытии страницы
window.addEventListener('beforeunload', () => {
    stopCommandSending();
    if (ws) {
        ws.close();
    }
});

