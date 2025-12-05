# Demo Flow (после реализации Telemetry/Metrics ingest)

Сценарий демонстрации должен показывать полный путь данных: от регистрации датчика и запуска эксперимента до потоковой/батчевой телеметрии и визуализации. После добавления ingest-компонентов используем следующий план.

## 1. Подготовка окружения

1. `docker-compose up -d` из `backend/services/experiment-service` (API + PostgreSQL).
2. `poetry run python bin/migrate.py --database-url <dsn>` — применить миграции.
3. `poetry run python bin/demo_seed.py` — создать `demo_project`, базовые эксперименты, датчики и тестовые токены.
4. Сохранить `project_id`, `owner_user_id`, выданные токены (`token_preview` виден в БД, полный токен выводится только при seed).

## 2. Регистрация датчика и профиля

```bash
curl -X POST http://localhost:8002/api/v1/sensors \
  -H "X-User-Id: <owner>" \
  -H "X-Project-Id: <project_id>" \
  -H "X-Project-Role: owner" \
  -H "Idempotency-Key: demo-sensor-1" \
  -d '{
        "project_id":"<project_id>",
        "name":"temperature_raw",
        "type":"thermocouple",
        "input_unit":"mV",
        "display_unit":"C",
        "conversion_profile":{
          "version":"v1",
          "kind":"linear",
          "payload":{"a":1.23,"b":0.2}
        }
      }'
```

Ответ содержит `sensor` и одноразовый `token`. Сохраняем их для CLI.

## 3. Эксперимент → запуск → capture session

1. `POST /api/v1/experiments` (с `project_id`, `name`, `tags`) → `experiment_id`.
2. `POST /api/v1/experiments/{experiment_id}/runs` → `run_id`.
3. `POST /api/v1/runs/{run_id}/capture-sessions` с `ordinal_number=1`, `status="running"` → `capture_session_id`.

В UI /tests можно проверить, что статус `running`.

## 4. Telemetry ingest

- CLI `demo_sensor.py` (добавить в `bin/`):

```bash
poetry run python bin/demo_sensor.py \
  --sensor-id <sensor_id> \
  --token <sensor_token> \
  --run-id <run_id> \
  --capture-session-id <session_id> \
  --mode stream \
  --duration 30
```

Внутри CLI:

```bash
curl -X POST http://localhost:8002/api/v1/telemetry \
  -H "Authorization: Bearer <sensor_token>" \
  -d '{
        "sensor_id":"...",
        "run_id":"...",
        "capture_session_id":"...",
        "readings":[
          {"timestamp":"2025-12-05T12:00:00Z","raw_value":1.23,"meta":{"step":1}}
        ]
      }'
```

Параллельно можно открыть `ws://localhost:8002/api/v1/telemetry/stream?sensor_id=...` (после реализации WebSocket/SSE) и показать лайв-график.

## 5. Метрики и остановка сессии

1. `POST /api/v1/runs/{run_id}/capture-sessions/{session_id}/stop` (`status="succeeded"`, `stopped_at=now`).
2. `POST /api/v1/runs/{run_id}/metrics`:

```bash
curl -X POST http://localhost:8002/api/v1/runs/<run_id>/metrics \
  -H "X-User-Id: <owner>" -H "X-Project-Id: <project_id>" -H "X-Project-Role: owner" \
  -d '{
        "metrics":[
          {"name":"loss","step":1,"value":0.42,"timestamp":"2025-12-05T12:00:00Z"},
          {"name":"loss","step":2,"value":0.31,"timestamp":"2025-12-05T12:01:00Z"}
        ]
      }'
```

3. `GET /api/v1/runs/{run_id}/metrics?name=loss` → убедиться, что массив точек возвращается.

## 6. Визуальный walkthrough

- В `frontend/apps/experiment-portal`:
  - Открыть список экспериментов, найти `demo_project`.
  - Зайти в run → переключиться между capture session, показать графики (при реализации WebSocket — лайв-график).
  - Показать журнал событий (capture start/stop, ingest, метрики).

## 7. Архивация и cleanup

1. `PATCH /api/v1/runs/{run_id}` (`status="succeeded"`).
2. `POST /api/v1/experiments/{experiment_id}/archive`.
3. (опционально) `POST /api/v1/runs:batch-status` для массовых переключений.

## Checklist для демо

- [ ] Миграции применены, seed выполнен.
- [ ] Есть свежие токены датчиков.
- [ ] CLI/скрипты для ingest и метрик работают.
- [ ] UI показывает данные (графики, списки).
- [ ] Имеется fallback (скрипты для повторного заполнения данных).

После реализации Telemetry/Metrics ingest и UI-графиков этот документ нужно дополнить скриншотами/реплей-скриптами.

