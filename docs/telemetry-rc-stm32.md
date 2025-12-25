# Телеметрия для RC моделей (машинка/самолёт) и STM32: соглашения и формат

Этот документ фиксирует рекомендации по тому, как отправлять телеметрию в текущую реализацию ingest в Experiment Service (`POST /api/v1/telemetry`).

## Цели
- **Поддержка STM32 + датчики**: частые измерения (IMU, GPS, напряжение, ток, температура, RPM).
- **Поддержка RC моделей**: запись телеметрии машинок и самолётов (включая RC каналы).
- **Минимум дублирования**: общие сведения об устройстве передаются один раз на пакет.

## Принцип `meta`: пакет + точка
Контракт ingest поддерживает:
- **top-level `meta`**: общие метаданные для всего пакета (приклеиваются к каждой точке).
- **`readings[].meta`**: метаданные конкретной точки (имеют приоритет при конфликте ключей).

Рекомендуемая схема:
- В top-level `meta` хранить: `vehicle_type`, `controller`, `board`, `fw_version`, `link_protocol`, `device_id`.
- В `readings[].meta` хранить: `signal` (обязательная для мультисигнальной телеметрии), и опционально `axis`, `frame`, `sample_hz`, `seq`.

## Рекомендуемые значения `vehicle_type`
- `rc_car`
- `rc_plane`
- `rc_drone` (если пригодится позднее)

## Примеры `signal` (readings[].meta.signal)
### RC car
- `battery.v`, `battery.a`, `battery.wh`
- `motor.rpm`, `motor.temp_c`
- `esc.temp_c`, `esc.current_a`
- `wheel.speed_mps`
- `rc.ch1_pwm` ... `rc.ch16_pwm`

### RC plane
- `gps.lat`, `gps.lon`, `gps.alt_m`
- `imu.ax`, `imu.ay`, `imu.az`
- `imu.gx`, `imu.gy`, `imu.gz`
- `baro.alt_m`, `airspeed.mps`
- `rc.ch1_pwm` ... `rc.ch16_pwm`

## Пример запроса (REST)
```bash
curl -X POST http://localhost:8002/api/v1/telemetry \
  -H "Authorization: Bearer <sensor_token>" \
  -d '{
        "sensor_id":"<sensor_id>",
        "run_id":"<run_id>",
        "capture_session_id":"<capture_session_id>",
        "meta":{
          "vehicle_type":"rc_plane",
          "controller":"stm32",
          "board":"f405",
          "fw_version":"0.3.1",
          "link_protocol":"crsf",
          "device_id":"plane-01"
        },
        "readings":[
          {"timestamp":"2025-12-20T12:00:00Z","raw_value":1500,"meta":{"signal":"rc.ch1_pwm"}},
          {"timestamp":"2025-12-20T12:00:00Z","raw_value":55.7558,"meta":{"signal":"gps.lat"}},
          {"timestamp":"2025-12-20T12:00:00Z","raw_value":37.6173,"meta":{"signal":"gps.lon"}}
        ]
      }'
```

## Примечания по хранению
- Каждая точка попадает в `telemetry_records` и хранится вместе с `meta` (JSONB), а также `raw_value` и (опционально) `physical_value`.
- Для преобразования `raw_value → physical_value` используются `conversion_profiles` (если активны для датчика).


