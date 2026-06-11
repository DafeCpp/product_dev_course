# Code Review: RC Vehicle Firmware

**Дата:** 2026-04-10 (обновлено 2026-06-10 — см. секцию «Review 2026-06-10» ниже)
**Scope:** `projects/rc_vehicle/firmware/` (common, esp32_common, esp32_s3, tests)

---

## Критические проблемы (исправить первыми)

| # | Проблема | Файл | Строки |
|---|----------|------|--------|
| 1 | ~~**Переполнение стека в HTTP POST-хэндлерах**~~ — FALSE POSITIVE: `ReadJsonBody()` уже проверяет `total_len >= buf_len` на строке 556 до начала записи | `esp32_common/http_server.cpp` | 556 |
| 2 | ~~**Race condition: статический буфер WebSocket**~~ — **ИСПРАВЛЕНО**: `static uint8_t buf` заменён на локальный. Также добавлен `NOLINT`-комментарий к `const_cast` в `WebSocketSendTelem` | `esp32_common/websocket_server.cpp` | 32, 123 |
| 3 | ~~**MockPlatform не совпадает с API**~~ — **ИСПРАВЛЕНО**: `MOCK_METHOD` и `FakePlatform`-методы обновлены на `Result<Unit, PlatformError>`; `InitPwm/Rc/Imu/Failsafe`, `SaveCalib`, `SaveStabilizationConfig`, `CreateTask` | `tests/mocks/mock_platform.hpp` | 29-32, 59-60, 113, 140-143 |
| 4 | **BUG: Ошибка при скачивании файлов телеметрии (`/api/log.bin`)** — при нажатии кнопки «CSV» скачивание завершается ошибкой. Появилась **недавно**, ранее работало корректно. Возможные причины: (а) несоответшение размера `TelemetryLogFrame` (128 байт `static_assert` vs JS-парсер с FIELD_OFFSETS до offset 124 + u8); (б) обрыв chunked HTTP-соединения при больших объёмах данных; (в) ring buffer пуст или PSRAM не выделилась при `TelemetryLog::Init()`; (г) ошибка в `VehicleControlGetLogFrame`/`VehicleControlGetLogInfo`/`VehicleControlGetEventCount`; (д) Section 2 (events) добавлен недавно — клиент может некорректно рассчитать `framesEnd` | `esp32_common/http_server.cpp` | 756–820 |

---

## Высокий приоритет

| # | Проблема | Файл | Строки |
|---|----------|------|--------|
| 4 | ~~**Quaternion singularity**~~ — **ИСПРАВЛЕНО**: перед нормализацией проверяется `qSqNorm < 1e-12f`; при singularity кватернион сбрасывается на единичный `(1,0,0,0)` | `common/madgwick_filter.cpp` | 83 |
| 5 | ~~**NVS Load без Clamp()**~~ — **ИСПРАВЛЕНО**: после `IsValid()` добавлен вызов `config.Clamp()` | `esp32_common/stabilization_config_nvs.cpp` | 32 |
| 6 | ~~**Race condition: WiFi status**~~ — FALSE POSITIVE: `portENTER_CRITICAL(&s_wifi_mux)` уже обёртывает `*out_status = s_sta_status` на строке 416; все сеттеры (`StaStatusSetConnected`, `StaStatusSetIp`, `StaStatusSetDisconnectReason`) тоже защищены тем же спинлоком | `esp32_common/wifi_ap.cpp` | 416 |
| 7 | ~~**WebSocket send не потокобезопасен**~~ — FALSE POSITIVE: паттерн `httpd_get_client_list` → `httpd_ws_get_fd_info` → `httpd_ws_send_data` — официально рекомендованный ESP-IDF v5.x способ рассылки из стороннего таска; stale FD фильтруется `httpd_ws_get_fd_info` (строки 132-135), ошибка отправки обрабатывается логом | `esp32_common/websocket_server.cpp` | 110-135 |

---

## Средний приоритет

| # | Проблема | Файл |
|---|----------|------|
| 8 | ~~**StabilizationManager: race condition на config_**~~ — **ИСПРАВЛЕНО**: добавлен `mutable std::mutex config_mutex_`; `GetConfig()` возвращает по значению; `SetConfig()`/`LoadFromNvs()` блокируют запись, `UpdateWeights()`/`ApplyConfig()` берут локальную копию под локом | `common/stabilization_manager.cpp` |
| 9 | ~~**Failsafe `GetTimeSinceLastActive()` без проверки переполнения**~~ — **ИСПРАВЛЕНО**: добавлена проверка `last_active_ms_ > now_ms`, при wraparound возвращается `UINT32_MAX` (аналогично `Update()`) | `common/failsafe.cpp:62-66` |
| 10 | ~~**`const_cast` на payload WebSocket**~~ — FALSE POSITIVE / уже смягчено: единственный caller (`SendTelem`) передаёт мутабельный `char buffer[1024]`, поэтому `const_cast` не UB; ESP-IDF `httpd_ws_send_data` payload не модифицирует; `NOLINTNEXTLINE` + комментарий добавлены в коммите deb82a6 | `esp32_common/websocket_server.cpp:127` |
| 11 | **NVS CalibBlob без версионирования** — при изменении структуры старые данные прочитаются некорректно | `esp32_common/imu_calibration_nvs.cpp` |
| 12 | **EKF: произвольный порог `S < 1e-9f`** — TODO_bugfixes.md рекомендует `S < params_.r_gz * 1e-3f` | `common/vehicle_ekf.cpp:102` |
| 13 | **CMakeLists.txt ссылается на `integration_tests`** — цель не определена, coverage с `ENABLE_COVERAGE=ON` упадёт | `tests/CMakeLists.txt:99-100` |

---

## Низкий приоритет / улучшения качества

| # | Проблема | Файл |
|---|----------|------|
| 14 | `RxBuffer::Advance()` молча теряет данные при переполнении | `common/uart_bridge_base.hpp:76` |
| 15 | Протокольные буферы фиксированного размера (`std::array<uint8_t, 32>`) — при увеличении payload получим переполнение; нужен `static_assert` | `common/uart_bridge_base.cpp` |
| 16 | `portMUX_TYPE` (спинлок) используется для защиты блокирующих WiFi-вызовов — следует заменить на FreeRTOS mutex | `esp32_common/wifi_ap.cpp:23` |
| 17 | Тест `FailsafeTest::TimeWrapAround` **не содержит ни одного ASSERT/EXPECT** | `tests/unit/test_failsafe.cpp:245-257` |
| 18 | Нет тестов: `stabilization_pipeline`, `vehicle_control_unified`, `imu_calibration`, `uart_bridge` | `tests/` |
| 19 | Параметрическая фикстура `RcVehicleParamTest` объявлена, но нигде не используется | `tests/fixtures/test_helpers.hpp` |

---

## Архитектурные рекомендации

### 1. Watchdog в control loop

Явно кормить IWDT или убедиться, что он включён в sdkconfig. Если control loop зависнет — устройство не перезагрузится.

### 2. Init-ready барьер

WebSocket-хэндлеры регистрируются в `main.cpp:56-88` до того, как control task гарантированно запущен. Команды могут придти в неинициализированное состояние. Решение — добавить флаг готовности или event group.

### 3. `std::span` вместо сырых указателей

Перевести массивные параметры на `std::span<const float, 3>` — вместо сырых указателей в `SetVehicleFrame()` и подобных API. Даёт bounds checking и самодокументирование.

### 4. Интеграционные тесты

Сейчас покрыт только `common/`, а `stabilization_pipeline`, `control_components`, `calibration_manager` не тестируются вообще. Нужны cross-component тесты.

### 5. Версионирование NVS-структур

Добавить поле `uint8_t version` в `CalibBlob` и `StabilizationConfig` для корректной миграции при обновлении прошивки.

### 6. Quaternion safety guard

```cpp
// После нормализации в madgwick_filter.cpp
if (qNorm < 1e-6f) {
    q0_ = 1.f; q1_ = 0.f; q2_ = 0.f; q3_ = 0.f;
    return;
}
```

### 7. HTTP body size validation

```cpp
// В начале каждого POST-хэндлера
if (req->content_len >= sizeof(body)) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body too large");
    return ESP_FAIL;
}
```

### 8. WebSocket buffer — убрать static

Заменить `static uint8_t buf[...]` на локальный буфер или добавить мьютекс для защиты при одновременных соединениях.

---

## Что сделано хорошо

- **`Result<T, E>` вместо исключений** — отличный подход для embedded
- **Platform abstraction** — чистое разделение common/esp32, легко портировать
- **Failsafe со state machine** — корректная реализация с мьютексом
- **Модульный control loop** — компоненты (`ImuHandler`, `WifiCommandHandler`, `TelemetryHandler`) независимы
- **EKF slip angle fix** — баг #4 из TODO уже исправлен (`kMinSpeedThreshold`)
- **`[[nodiscard]]`** на Result-типах и query-методах
- **500 Гц loop с `vTaskDelayUntil()`** — корректная синхронизация с 1 мс тиком FreeRTOS
- **CRC16 CCITT** — корректная реализация протокола
- **RAII** — правильное управление ресурсами, деструкторы, удалённые move-конструкторы
- **Const-correctness** — последовательное использование const в query-методах
- **Anti-windup** в PID-контроллере
- **Bilinear transform** в Butterworth LPF — корректная реализация

---

# Review 2026-06-10

**Scope:** control path (control loop, failsafe, Madgwick, EKF, стабилизация), WS/UDP-слой, протокол.
Статус всех пунктов: **OPEN** (не исправлено на момент ревью).
**Задачи:** каждый пункт оформлен отдельным файлом в [`tasks/`](tasks/README.md) (FW-R* / FW-RF*), там же план реализации по этапам.

## Баги

| # | Приоритет | Проблема | Файл | Строки |
|---|-----------|----------|------|--------|
| R1 | **HIGH (safety)** | **Failsafe-нейтраль перезаписывается trim'ом.** В `Step()` после `HandleFailsafe()` безусловно вызывается `UpdatePwm()`: `HandleFailsafe()` ставит `SetPwmNeutral()`, но в той же итерации `UpdatePwm()` выполняет `SetPwm(0 + throttle_trim, 0 + steering_trim)`. При ненулевом `throttle_trim` во время потери сигнала моторы получают trim вместо нейтрали — машина может ползти. Fix: ранний выход из `Step()` при активном failsafe (или флаг, пропускающий `UpdatePwm`/`UpdateStabilization`) | `common/control_loop_processor.cpp` | 33-34, 134, 157-159 |
| R2 | **HIGH** | **OversteerGuard: ложный всплеск slip_rate после простоя.** В ветке «малая скорость / малый yaw rate» сбрасывается `prev_slip_deg_ = 0.0f`. На первом тике после превышения порогов `slip_rate = (slip − 0) / 0.002` — гигантское значение, условие `rate_thresh_deg_s` выполняется всегда → детекция вырождается в проверку одного `slip_thresh_deg`, ложные срабатывания (и сброс газа) при входе в поворот. Fix: сохранять `prev_slip_deg_ = slip` и/или пропускать первый тик после реактивации | `common/stabilization_pipeline.cpp` | 137-152 |
| R3 | **MEDIUM** | **Смесь систем координат в mag → Madgwick.** При валидной mag-калибровке в `UpdateWithMag` передаётся `(px, py, dot_n)`: `px, py` — x/y-компоненты проекции вектора на калибровочную плоскость **в СК датчика**, `dot_n` — скаляр вдоль нормали плоскости. Корректно только если нормаль ≈ ось Z датчика; при наклонном монтаже IMU (который vehicle-frame-механизм специально поддерживает) yaw искажается. Madgwick 9DOF сам устраняет склонение (`bx = sqrt(hx²+hy²)`) — передавать полный калиброванный вектор `mag_cal.mx/my/mz`, как в ветке без калибровки. Проверить на реальных данных с наклонным монтажом | `common/control_components.cpp` | 178-179 |
| R4 | **LOW** | **«Защита от переполнения» в Failsafe срабатывает наоборот.** Беззнаковое `now_ms - last_active_ms_` само корректно обрабатывает wrap uint32 (~49.7 суток аптайма); явная проверка `last_active_ms_ > now_ms` превращает корректные «прошло 11 мс» в «таймаут истёк» → ложный failsafe в момент переполнения. ⚠️ Конфликтует с пунктом 9 ревью 2026-04-10, где эта проверка была *добавлена* как фикс. Направление ошибки безопасное (failsafe лишний раз сработает), поэтому LOW — но математически проверка не нужна и вредна (то же в `GetTimeSinceLastActive`) | `common/failsafe.cpp` | 37-38, 67 |
| R5 | **LOW** | **MotionDriver: код противоречит комментарию.** Комментарий «Минимальный рабочий газ (только для LinearRamp)», но условие не проверяет `accel_mode` и применяется и в PID-режиме, где может подбросить throttle поверх PI-коррекции. Добавить проверку режима или исправить комментарий | `common/motion_driver.cpp` | 104-108 |
| R6 | **MEDIUM** | **Гонка в двойной буферизации WS-телеметрии.** Очередь длины 1 + 2 буфера не защищают от медленного клиента: пока `telem_sender_task` шлёт из `buf[0]`, два следующих `WebSocketEnqueueTelem` (20 Гц) снова пишут в `buf[0]` → рваный JSON при тормозящем `httpd_ws_send_data` | `esp32_common/websocket_server.cpp` | 183-200 |
| R7 | **LOW** | **Молчаливое усечение hz/port в WS-команде.** `(uint8_t)hz_item->valueint` до валидации: `hz=266` → `10` и молча принимается как валидное (`is_valid_hz` в UDP-слое проверяет уже усечённое значение). Аналогично port → uint16_t. Валидировать в int до каста | `esp32_s3/main/ws_command_handlers.cpp` | 711-715 |
| R8 | **LOW** | **`Protocol::next_command_seq_` — static, не атомарный.** Общий мутируемый счётчик для всех вызовов `BuildCommand` без синхронизации; гонка при сборке команд из двух задач, плюс seq общий для всех инстансов | `common/protocol.cpp` | 11 |

## Рефакторинг

| # | Предложение | Файл |
|---|-------------|------|
| RF1 | **Дедупликация WS-хендлеров** (842 строки): каждый из ~25 хендлеров повторяет скелет `cJSON_CreateObject` → `AddString("type", …)` → `WsSendJsonReply` → `cJSON_Delete`. RAII-обёртка над `cJSON` + хелпер `SendAck(req, type, fill_fn)` и `GetFloat/GetInt(json, key, default)` сократят файл вдвое и уберут риск утечек при ранних return | `esp32_s3/main/ws_command_handlers.cpp` |
| RF2 | **Общая валидация кадров протокола**: пять `Parse*`-функций дословно повторяют блок header/length/CRC (~30 строк каждая) → `ValidateFrame(buffer, expected_type, expected_len)` | `common/protocol.cpp` |
| RF3 | **`ImuHandler::Update` (~140 строк)**: проекция магнитометра на калибровочную плоскость продублирована дважды (строки 140-150 и 171-177) → `ProjectMagOntoCalibPlane()`; метод разбить на чтение IMU / mag-heading / Madgwick-фидинг | `common/control_components.cpp` |
| RF4 | **Kids-пресеты — единый источник истины**: `HandleGetKidsPresets` хардкодит значения в JSON вручную, дублируя `KidsConfig::ApplyPreset` → таблица структур + цикл | `esp32_s3/main/ws_command_handlers.cpp:310-368` |
| RF5 | **Копирование конфига под мьютексом на 500 Гц**: за итерацию `Step()` конфиг копируется минимум трижды (`GetConfig` в `Step`, ещё раз в `UpdateWeights`, плюс диагностика) — ~1500 копий крупной структуры/с и contention с WS-задачей. Брать один snapshot в начале итерации и передавать вниз параметром | `common/control_loop_processor.cpp`, `common/stabilization_manager.cpp` |
| RF6 | **`SetConfig` vs `ApplyConfig`**: блок «применить beta/LPF/gains к фильтрам» продублирован (строки 80-92 и 148-157) → extract; magic `0x53544232` в логе захардкожен вместо константы | `common/stabilization_manager.cpp` |
| RF7 | **`slew_rate.hpp`**: вне `namespace rc_vehicle`, устаревший комментарий «main loop RP2040/STM32» (платформа давно ESP32) | `common/slew_rate.hpp` |
