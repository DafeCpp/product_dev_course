# Code Review: RC Vehicle Firmware

**Дата:** 2026-04-10
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
