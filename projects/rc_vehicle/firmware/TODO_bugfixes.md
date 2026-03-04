# Firmware Bug Fixes — Remaining

Сделано: #1 (TelemetryLog race condition), #2 (EKF P matrix clamp).

---

## #3 HIGH — Failsafe не потокобезопасен

**Файл:** `common/failsafe.cpp` / `common/failsafe.hpp`

`last_active_ms_`, `state_`, `initialized_` читаются и пишутся из разных FreeRTOS-задач без синхронизации.

**Исправление:** добавить `mutable std::mutex mutex_` (аналогично TelemetryLog), взять lock в `Update()`, `IsTriggered()`, `Reset()`.

---

## #4 HIGH — Нестабильный угол заноса при нулевой скорости

**Файл:** `common/vehicle_ekf.cpp:140-142`

```cpp
float VehicleEkf::GetSlipAngleRad() const noexcept {
    return std::atan2(x_[1], x_[0]);  // нестабильно при vx≈0, vy≈0
}
```

EKF накапливает дрейф — `vx` и `vy` редко точно равны нулю на стоянке, поэтому `atan2` даёт нестабильное значение.

**Исправление:** проверять `GetSpeedMs() < kMinSpeedThreshold` (например, `0.05f` м/с) и возвращать `0.0f`.

---

## #5 MEDIUM — Переполнение uint32_t в расчёте dt_ms

**Файл:** `common/vehicle_control_unified.cpp` (строки с `now - last_loop`)

После ~49 дней работы `millis()` (или `platform_->GetTimeMs()`) переполняется — один цикл получает огромный `dt_ms`.

**Исправление:** вычислять через явное приведение:
```cpp
const uint32_t dt_ms = static_cast<uint32_t>(now - last_loop_ms_);
```
Subtraction с беззнаковым переполнением даёт корректный результат автоматически, если оба операнда `uint32_t`.

---

## #6 MEDIUM — NVS Load не вызывает Clamp()

**Файл:** `esp32_common/stabilization_config_nvs.cpp:31-42`

После `config.IsValid()` нужно вызвать `config.Clamp()`: данные в NVS могли быть записаны старой прошивкой с другим диапазоном допустимых значений.

**Исправление:**
```cpp
if (config.IsValid()) {
    config.Clamp();   // ← добавить
    *out = config;
    return ESP_OK;
}
```

---

## #7 MEDIUM — LPF Butterworth: граничные частоты

**Файл:** `common/lpf_butterworth.cpp:16-26`

`tan(pi * fc / fs)` при `fc == fs/2` (Найквист) даёт `+inf`. Нет проверки на минимальную частоту среза.

**Исправление:** в `SetParams()` добавить guard:
```cpp
if (cutoff_hz <= 0.0f || cutoff_hz >= sample_rate_hz * 0.5f) {
    return false;  // или clamp
}
```

---

## #8 LOW — RxBuffer::Advance() молча обрезает данные

**Файл:** `common/uart_bridge_base.hpp:74-77`

```cpp
void Advance(size_t n) noexcept {
    pos_ += n;
    if (pos_ > CAPACITY) pos_ = CAPACITY;  // молчаливая потеря данных
}
```

**Исправление:** добавить `assert(pos_ + n <= CAPACITY)` в debug-сборке, или вернуть `bool`.

---

## #9 LOW — Контроллеры: nullptr не проверяется в Init()

**Файл:** `common/stabilization_pipeline.cpp:12-18`

`YawRateController::Init()`, `PitchCompensator::Init()` и другие принимают указатели без проверки на `nullptr`. Проверка есть только в `Process()`.

**Исправление:** добавить `assert(ptr != nullptr)` или ранний возврат в `Init()`.

---

## #10 LOW — Произвольный порог в UpdateGyroZ

**Файл:** `common/vehicle_ekf.cpp:101`

```cpp
if (S < 1e-9f) return;  // порог выбран без обоснования
```

При очень маленьком `r_gz` S может быть меньше `1e-9` и легитимно. Порог стоит привязать к масштабу задачи.

**Исправление:** заменить на `if (S < params_.r_gz * 1e-3f)` или документировать обоснование константы.
