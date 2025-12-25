# telemetry_cli

CLI/agent для **универсального** сбора телеметрии (реальные источники + синтетические сигналы) и отправки в **Experiment Service**:
`POST /api/v1/telemetry`.

Поддерживает источники:
- `synthetic` — генератор (sine/saw/square/noise/constant)
- `udp_json` — UDP JSON (удобно как шлюз от ESP32/STM32)
- `esp32_ws` — WebSocket-клиент под JSON телеметрии (черновик из `projects/rc_vehicle/docs/interfaces_protocols.md`)

## Установка

```bash
cd projects/telemetry_cli
python3 -m venv .venv
source .venv/bin/activate
pip install -U pip
pip install -e .
```

## Запуск
1) Возьмите пример конфига:

```bash
cp configs/example.synthetic.yaml /tmp/telemetry.yaml
```

2) Заполните:
- `experiment_service.sensor_token` (выдаётся при `POST /api/v1/sensors`)
- `target.sensor_id`
- опционально `target.run_id`, `target.capture_session_id`

3) Запуск:

```bash
telemetry-cli --config /tmp/telemetry.yaml
```

## UDP JSON формат
Агент слушает UDP (см. `configs/example.udp_json.yaml`) и принимает:
- одиночную точку:
  `{"ts_ms": 1734690000000, "signal": "imu.ax", "raw_value": 0.01, "meta": {"seq":123}}`
- мультисэмпл:
  `{"ts_ms": 1734690000000, "values": {"imu.ax": 0.01, "imu.ay": 0.02}, "meta": {"seq":123}}`


