# RC-специфичный код для ESP32 прошивки

Переиспользуемая ESP32-веб-инфраструктура (softAP, captive portal, HTTP-каркас,
WebSocket) вынесена в `projects/firmware_common/esp32/` (LOS-184) — её
переиспользует и голова стенда (bench-head). Этот каталог содержит только
RC-специфичный остаток:

- `rc_http_routes.*` — веб-страница управления (`web/`, вшита через `#embed`),
  `/api/log.bin` (бинарный лог телеметрии), `/api/crash.json`
- `crash_logger.*` — сохранение причины перезагрузки в NVS
- `udp_telem_sender.*` — UDP-стриминг телеметрии (для инструментов вне WS)
- `imu_calibration_nvs.*`, `mag_calibration_nvs.*`, `stabilization_config_nvs.*`
  — хранение калибровок/конфигурации в NVS
- `mmc5983_i2c.*` — I2C-драйвер магнитометра

Статические файлы веб‑интерфейса:

- `web/index.html`
- `web/style.css`
- `web/app.js`

Команда управления (throttle/steering, тип "cmd") и остальные JSON-команды
регистрируются в `WsCommandRegistry` (`esp32_s3/main/ws_command_registry.*`) —
общий `websocket_server` знает только про один колбэк
`WebSocketSetJsonHandler(...)` на все типы кадров.
