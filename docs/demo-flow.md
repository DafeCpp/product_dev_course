# Demo Flow (MVP, “для людей”)

Короткий сценарий демонстрации MVP: за одну команду поднимаем окружение и генерируем “живые” данные (проект → эксперимент → run → capture session → telemetry), дальше показываем это в UI и, при необходимости, подтверждаем через Grafana/DB.

## 0. Что должно быть запущено

Порты:
- Auth Service: `8001`
- Experiment Service: `8002`
- Telemetry Ingest Service: `8003`
- Auth Proxy: `8080`
- Experiment Portal: `3000` (dev) / `80` (prod)

## 1. Подготовка данных одной командой

В корне репозитория:

```bash
make mvp-demo-check
```

Команда:
- поднимет dev-стек (`make dev-up`);
- применит миграции (`make auth-init`, `make experiment-migrate`);
- создаст пользователя + проект + эксперимент + run + capture session + датчик;
- отправит 1 точку телеметрии через `Telemetry Ingest` (`POST http://localhost:8003/api/v1/telemetry`);
- проверит, что `sensors.last_heartbeat` обновился и в `telemetry_records` есть строки.

Важно:
- Скрипт выводит в консоль `username`, `user_id`, `project_id`, `experiment_id`, `run_id`, `capture_session_id`, `sensor_id`.
- Пароль пользователя в демо‑прогоне: `demo12345`.

## 2. Демонстрация в UI (5–7 минут)

1. Откройте портал: `http://localhost:3000`
2. Войдите пользователем из вывода `make mvp-demo-check` (пароль `demo12345`).
3. Откройте список проектов и убедитесь, что есть проект `mvp-demo-<timestamp>`.
4. Перейдите в список экспериментов и выберите этот проект.
5. Откройте эксперимент `MVP Demo Experiment <timestamp>`.
6. Перейдите в созданный run `run-1`.
7. Убедитесь, что есть capture session со статусом `running` (ordinal `1`).
8. Перейдите в Sensors → откройте `temperature_raw` → убедитесь, что:
   - `last_heartbeat` не пустой (обновился после ingest),
   - (опционально) в форме “тестовая отправка” можно отправить ещё 1–2 точки.

Ожидаемый результат:
- В UI виден полный “сквозной” путь данных: проект → эксперимент → запуск → сессия → датчик с обновлённым heartbeat.

## 3. Если задают вопросы “а данные точно в системе?”

### Быстрая проверка Telemetry Ingest

```bash
curl -sf http://localhost:8003/health
```

### Быстрая проверка в БД (telemetry_records)

Подставьте `sensor_id` из вывода `make mvp-demo-check`:

```bash
docker-compose exec -T postgres psql -U postgres -d experiment_db -c \
  "select count(*) from telemetry_records where sensor_id='<sensor_id>';"
```

### Логи в Grafana (если нужно)

- Grafana: `http://localhost:3001` (admin/admin)
- Пример запроса: `{service="telemetry-ingest-service"}` или `{service="experiment-service"}`

## 4. Troubleshooting

- Если `experiment-portal` не поднимается / была ошибка `ContainerConfig`:
  - `make dev-fix` (почистит проблемные контейнеры и поднимет стек заново).
- Если после `make mvp-demo-check` в UI “не видно данных”:
  - обновите страницу;
  - проверьте, что выбран нужный проект (`mvp-demo-<timestamp>`).

