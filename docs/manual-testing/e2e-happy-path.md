# E2E happy-path: Portal → Auth Proxy → Services → DB

Документ — практический чеклист “пройти полный путь руками”: поднять стек, залогиниться в Portal через `auth-proxy`, создать сущности (project/experiment/run/capture session/sensor), отправить телеметрию и проверить результат в UI и в PostgreSQL.

См. также:
- [`README.md`](README.md) — индекс сценариев ручного тестирования.
- [`auth-flow.md`](auth-flow.md) — сценарии аутентификации (логин/логаут/защита роутов).
- [`smoke-navigation.md`](smoke-navigation.md) — smoke-проверка загрузки всех разделов.
- `docs/demo-flow.md` — короткий сценарий демо (5–7 минут, минимум шагов).
- `docs/mvp-acceptance-checklist.md` — технический чеклист приемки MVP.

## 0) Предусловия

- Docker + `docker-compose`
- `make`
- Порты свободны: `3000`, `8001`, `8002`, `8003`, `8080`, `5433`, `3001`

## 1) Поднять окружение

Из корня репозитория:

```bash
cp env.docker.example .env
cp docker-compose.override.yml.example docker-compose.override.yml
make dev-up
```

Проверки:

```bash
curl -fsS http://localhost:8001/health && echo "auth-service ok"
curl -fsS http://localhost:8002/health && echo "experiment-service ok"
curl -fsS http://localhost:8003/health && echo "telemetry-ingest ok"
curl -fsS http://localhost:8080/health && echo "auth-proxy ok"
```

UI:
- Portal: `http://localhost:3000`
- Grafana: `http://localhost:3001` (по умолчанию `admin/admin`)

## 2) Создать пользователя (если нужно)

В Portal пока нет страницы регистрации, поэтому пользователя проще создать через Auth Service:

```bash
TS="$(date +%s)"
USERNAME="manual${TS}"
EMAIL="manual${TS}@example.com"
PASSWORD="manual12345"

curl -fsS -X POST http://localhost:8001/auth/register \
  -H 'Content-Type: application/json' \
  -d "{\"username\":\"${USERNAME}\",\"email\":\"${EMAIL}\",\"password\":\"${PASSWORD}\"}"
echo
echo "Created user: ${USERNAME} / ${PASSWORD}"
```

Альтернатива: использовать дефолтного пользователя, который выводится в `make dev-up` (обычно `admin/admin123`), но он может требовать смену пароля при первом входе.

## 3) Войти в Portal (через Auth Proxy, cookie-based)

1. Откройте `http://localhost:3000`
2. Перейдите на `/login` (Portal сам редиректит туда, если вы не авторизованы)
3. Введите `USERNAME/PASSWORD` из шага выше

Ожидание:
- после успешного логина появляются защищённые страницы (`/projects`, `/experiments`, `/sensors`)
- в браузере появляются cookies от `auth-proxy`, включая `csrf_token` (НЕ HttpOnly)

## 4) Проекты: создать и “активировать” проект

1. Откройте страницу Projects: `http://localhost:3000/projects`
2. Нажмите “Create” / “Новый проект” (название зависит от UI)
3. Создайте проект (например `Manual Project`)
4. Убедитесь, что проект выбран/активирован (Portal использует активный `project_id` для запросов к Experiment Service)

Ожидание:
- проект появляется в списке
- дальнейшие запросы (Experiments/Sensors) работают в контексте выбранного проекта

## 5) Sensors: создать сенсор и сохранить токен

1. Откройте `http://localhost:3000/sensors`
2. Нажмите “New sensor” / “Создать сенсор”
3. Заполните поля (пример):
   - name: `temperature_raw`
   - type: `thermocouple`
   - input_unit: `mV`
   - display_unit: `C`
4. После создания UI должен показать **sensor token** (одноразовая выдача) — скопируйте его.

Ожидание:
- сенсор в списке
- в деталях сенсора `last_heartbeat` пустой (до первой телеметрии)

## 6) Experiments: создать эксперимент → run

1. Откройте `http://localhost:3000/experiments`
2. Создайте эксперимент (например `Manual Experiment`)
3. Откройте созданный эксперимент
4. Создайте run (например `run-1`)
5. Перейдите в run (страница вида `http://localhost:3000/runs/<run_id>`)

Ожидание:
- эксперимент виден в списке, открывается детальная страница
- run создаётся и открывается

## 7) Capture session: старт/стоп “отсчёта”

На странице run:

1. На странице run найдите кнопку **“Старт отсчёта”** (в правой части шапки run, рядом со статусом).
   Если у вас run в статусе **«Черновик»**, эта кнопка всё равно должна быть доступна (Portal разрешает создавать capture session для `draft`/`running`).
   Альтернатива: в блоке **«Сессии отсчёта»** можно нажать **«Создать сессию»**.
2. Убедитесь, что в блоке Capture Sessions появилась активная сессия
3. (Опционально) Нажмите **“Остановить отсчёт”** → статус меняется на `succeeded`/`failed` в зависимости от выбора UI

Ожидание:
- `capture_session_id` появляется в списке сессий

## 8) Отправить телеметрию (curl → telemetry-ingest-service)

Нужно:
- `SENSOR_ID` (из UI сенсора)
- `SENSOR_TOKEN` (скопирован на шаге 5)
- `RUN_ID` и `CAPTURE_SESSION_ID` (из страницы run / capture sessions)

Пример запроса (замените плейсхолдеры):

```bash
curl -fsS -X POST http://localhost:8003/api/v1/telemetry \
  -H "Authorization: Bearer <SENSOR_TOKEN>" \
  -H 'Content-Type: application/json' \
  -d "{
    \"sensor_id\": \"<SENSOR_ID>\",
    \"run_id\": \"<RUN_ID>\",
    \"capture_session_id\": \"<CAPTURE_SESSION_ID>\",
    \"meta\": {\"source\": \"manual\"},
    \"readings\": [
      {\"timestamp\": \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\", \"raw_value\": 1.23, \"meta\": {\"step\": 1}}
    ]
  }"
echo
```

Ожидание:
- ответ вида `{ "status": "accepted", "accepted": 1 }` (код может быть `200` или `202`)

## 9) Проверки результата

### 9.1) В UI

1. Откройте страницу сенсора (`/sensors` → выбрать сенсор)
2. Убедитесь, что `last_heartbeat` обновился (не пустой)

### 9.2) В БД (Postgres)

Проверить, что запись реально появилась в `telemetry_records`:

```bash
docker-compose exec -T postgres psql -U postgres -d experiment_db -c \
  "select count(*) from telemetry_records where sensor_id='<SENSOR_ID>';"
```

Ожидание: `count >= 1`.

## 10) Негативная проверка: CSRF в Auth Proxy

Auth Proxy использует double-submit cookie:
- после `POST /auth/login` выставляется cookie `csrf_token` (НЕ HttpOnly)
- для **POST/PUT/PATCH/DELETE** запросов при наличии session cookies нужен заголовок `X-CSRF-Token` равный `csrf_token`

Быстрый тест (проверяем именно поведение прокси — upstream может вернуть другую ошибку, но **не 403 от CSRF**):

1) Без `X-CSRF-Token` (ожидаем `403`):

```bash
curl -i -sS -X POST http://localhost:8080/api/any/state-changing \
  -H 'Content-Type: application/json' \
  --cookie "access_token=dummy; csrf_token=dummy" \
  -d '{"ping":1}' | head -n 20
```

2) С `X-CSRF-Token` (CSRF-барьер снят; дальше ответ зависит от upstream):

```bash
curl -i -sS -X POST http://localhost:8080/api/any/state-changing \
  -H 'Content-Type: application/json' \
  -H 'X-CSRF-Token: dummy' \
  --cookie "access_token=dummy; csrf_token=dummy" \
  -d '{"ping":1}' | head -n 20
```

Важно: в реальном браузерном флоу `csrf_token` берётся из cookie автоматически (см. `src/utils/csrf.ts` и интерсепторы axios).

## 11) Troubleshooting

- **docker-compose ошибка `ContainerConfig`**:
  - `make dev-fix`
- **Portal не видит данные / 403**:
  - проверьте, что выбран/активирован нужный проект в UI
  - обновите страницу и повторите запрос
- **403 CSRF от `auth-proxy`**:
  - проверьте наличие cookie `csrf_token` и заголовка `X-CSRF-Token` на state-changing запросах

