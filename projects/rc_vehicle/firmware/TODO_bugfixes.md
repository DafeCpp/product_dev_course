# Firmware Bug Fixes

Все баги закрыты. История исправлений:

| # | Приоритет | Баг | Исправление |
|---|-----------|-----|-------------|
| #1 | HIGH | TelemetryLog race condition | mutex в `TelemetryLog` |
| #2 | HIGH | EKF P matrix clamp | `ClampP()` в `vehicle_ekf.cpp` |
| #3 | HIGH | Failsafe не потокобезопасен | `mutable std::mutex mutex_` в `failsafe.hpp:101`, lock во всех публичных методах |
| #4 | HIGH | Нестабильный угол заноса при v≈0 | Проверка `GetSpeedMs() < kMinSpeedThreshold (0.3f)` в `vehicle_ekf.cpp:196` |
| #5 | MEDIUM | Переполнение uint32_t в dt_ms | `uint32_t` вычитание с wrap-around в `vehicle_control_unified.cpp:46` |
| #6 | MEDIUM | NVS Load без Clamp() | `config.Clamp()` после `IsValid()` в `stabilization_config_nvs.cpp:62` |
| #7 | MEDIUM | LPF Butterworth: нет guard при fc ≥ fs/2 | `if (cutoff_hz <= 0.f \|\| cutoff_hz >= sample_rate_hz / 2.f) return false` в `lpf_butterworth.cpp:17` |
| #8 | LOW | RxBuffer::Advance молча обрезал данные | `assert(pos_ + n <= CAPACITY)` в `uart_bridge_base.hpp:76` |
| #9 | LOW | nullptr не проверялся в Init() | `assert(ptr != nullptr)` в `stabilization_pipeline.cpp:15, 59, 88` |
| #10 | LOW | Порог 1e-9f в UpdateGyroZ необоснован | Заменён на `kPDiagMin = 1e-6f` (обоснован гарантией `ClampP()`), `vehicle_ekf.cpp:112` |
