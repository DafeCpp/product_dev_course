# Баги — единый список

Все известные баги проекта. Здесь же заводить новые.

---

## Frontend (Experiment Portal)

### BUG-F-001 — Навигация через меню не работает на мобильном браузере
**Приоритет:** HIGH
**Статус:** [x] Исправлен

При тапе на пункт меню с мобильного браузера страница не меняется.

**Возможные причины:** обработчики click/touch, поведение React Router на мобильных браузерах.

---

### BUG-F-005 — Панель телеметрии падает с "Failed to construct 'URL': Invalid URL"
**Приоритет:** HIGH
**Статус:** [x] Исправлен

При открытии панели телеметрии возникает ошибка:
```
Failed to construct 'URL': Invalid URL
```

URL для SSE/WebSocket-эндпоинта собирается из невалидного или пустого значения (скорее всего `undefined` или пустая строка подставляется вместо хоста/пути).

**Возможные причины:**
- Переменная окружения `VITE_API_URL` / `VITE_WS_URL` не задана или пустая
- `sensor_id`, `experiment_id` или другой параметр пути ещё не загружен в момент построения URL
- Неверный базовый URL в конфиге axios/fetch для данного окружения

---

### BUG-F-004 — Live telemetry (SSE) не работает при открытии через информацию о датчике
**Приоритет:** HIGH
**Статус:** [x] Исправлен (та же причина что BUG-F-005)
**Файл:** `projects/frontend/apps/experiment-portal/src/components/SensorDetailModal.tsx` (предположительно)

При открытии Live telemetry через страницу/модал с информацией о датчике SSE-стрим не запускается или не отображает данные.

**Возможные причины:**
- Неверный `sensor_id` или `capture_session_id` передаётся в SSE-эндпоинт
- SSE-соединение открывается до того, как компонент получил нужные данные
- Проблема с авторизацией SSE-запроса (токен не передаётся)

---

### BUG-F-003 — Не работает кнопка копирования токена датчика
**Приоритет:** HIGH
**Статус:** [x] Исправлен

Кнопка копирования токена датчика не работает. Вероятно та же причина, что и BUG-F-002 — `navigator.clipboard` недоступен вне HTTPS.

---

### BUG-F-002 — `navigator.clipboard.writeText` падает с TypeError
**Приоритет:** HIGH
**Статус:** [x] Исправлен
**Файл:** `projects/frontend/apps/experiment-portal/src/pages/AdminUsers.tsx:112,120,269`

```
Uncaught TypeError: Cannot read properties of undefined (reading 'writeText')
    at handleCopyInviteLink / handleCopyToken / onClick
```

`navigator.clipboard` доступен только в **безопасном контексте** (HTTPS или localhost). При работе через HTTP (например, по локальной сети или staging без TLS) объект `undefined`.

Затронуто три места:
- `handleCopyToken` — копирование токена приглашения
- `handleCopyInviteLink` — копирование ссылки приглашения
- `onClick` на сброшенном пароле

**Исправление:** добавить фолбэк через `document.execCommand('copy')` или показывать ошибку если буфер обмена недоступен:
```ts
if (navigator.clipboard) {
    await navigator.clipboard.writeText(text)
} else {
    // fallback или toast с текстом для ручного копирования
}
```

---

## Observability / Grafana

### BUG-O-001 — В Grafana (прод) не работают логи, нельзя выбрать лейблы
**Приоритет:** HIGH
**Статус:** [x] Исправлен
**Среда:** production

В Grafana Explore / Logs нет доступных лейблов — источник логов (Loki) либо не подключён, либо не передаёт данные. Логи не отображаются.

**Возможные причины:**
- Alloy не запущен или не достигает Loki в проде (неверный адрес, firewall)
- Datasource Loki в Grafana не настроен для prod-окружения
- Loki не получает данные от сервисов (неверный endpoint в конфиге Alloy/sidecar)
- Loki запущен, но retention/ingestion pipeline сломан

**Что проверить:**
1. Статус контейнеров Loki и Alloy в проде
2. Настройки datasource в Grafana (`/datasources`)
3. Логи самого Alloy на наличие ошибок отправки
4. Сетевую доступность порта 3100 (Loki) из Alloy

---

## Firmware (RC Vehicle)

### BUG-RC-003 — Failsafe не потокобезопасен
**Приоритет:** HIGH
**Статус:** [ ] Открыт
**Файл:** `projects/rc_vehicle/firmware/common/failsafe.cpp`

`last_active_ms_`, `state_`, `initialized_` читаются и пишутся из разных FreeRTOS-задач без синхронизации.

**Исправление:** добавить `mutable std::mutex mutex_`, взять lock в `Update()`, `IsTriggered()`, `Reset()`.

---

### BUG-RC-004 — Нестабильный угол заноса при нулевой скорости
**Приоритет:** HIGH
**Статус:** [ ] Открыт
**Файл:** `projects/rc_vehicle/firmware/common/vehicle_ekf.cpp:140-142`

```cpp
return std::atan2(x_[1], x_[0]);  // нестабильно при vx≈0, vy≈0
```

**Исправление:** вернуть `0.0f` при `GetSpeedMs() < 0.05f`.

---

### BUG-RC-005 — Переполнение uint32_t в расчёте dt_ms
**Приоритет:** MEDIUM
**Статус:** [ ] Открыт
**Файл:** `projects/rc_vehicle/firmware/common/vehicle_control_unified.cpp`

После ~49 дней работы `millis()` переполняется — один цикл получает огромный `dt_ms`.

**Исправление:** `const uint32_t dt_ms = static_cast<uint32_t>(now - last_loop_ms_);`

---

### BUG-RC-006 — NVS Load не вызывает Clamp()
**Приоритет:** MEDIUM
**Статус:** [ ] Открыт
**Файл:** `projects/rc_vehicle/firmware/esp32_common/stabilization_config_nvs.cpp:31-42`

Данные из NVS могут быть из старой прошивки с другим диапазоном значений — `Clamp()` не вызывается.

**Исправление:** добавить `config.Clamp()` после `config.IsValid()`.

---

### BUG-RC-007 — LPF Butterworth: нет guard на граничные частоты
**Приоритет:** MEDIUM
**Статус:** [ ] Открыт
**Файл:** `projects/rc_vehicle/firmware/common/lpf_butterworth.cpp:16-26`

`tan(pi * fc / fs)` при `fc == fs/2` даёт `+inf`.

**Исправление:**
```cpp
if (cutoff_hz <= 0.0f || cutoff_hz >= sample_rate_hz * 0.5f) return false;
```

---

### BUG-RC-008 — RxBuffer::Advance() молча обрезает данные
**Приоритет:** LOW
**Статус:** [ ] Открыт
**Файл:** `projects/rc_vehicle/firmware/common/uart_bridge_base.hpp:74-77`

```cpp
if (pos_ > CAPACITY) pos_ = CAPACITY;  // молчаливая потеря данных
```

**Исправление:** `assert(pos_ + n <= CAPACITY)` в debug-сборке.

---

### BUG-RC-009 — nullptr не проверяется в Init() контроллеров
**Приоритет:** LOW
**Статус:** [ ] Открыт
**Файл:** `projects/rc_vehicle/firmware/common/stabilization_pipeline.cpp:12-18`

`YawRateController::Init()`, `PitchCompensator::Init()` принимают указатели без проверки на `nullptr`.

**Исправление:** `assert(ptr != nullptr)` в `Init()`.

---

### BUG-RC-010 — Произвольный порог в UpdateGyroZ
**Приоритет:** LOW
**Статус:** [ ] Открыт
**Файл:** `projects/rc_vehicle/firmware/common/vehicle_ekf.cpp:101`

```cpp
if (S < 1e-9f) return;  // порог выбран без обоснования
```

**Исправление:** заменить на `if (S < params_.r_gz * 1e-3f)` или задокументировать константу.
