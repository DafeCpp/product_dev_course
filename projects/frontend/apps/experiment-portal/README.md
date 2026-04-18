# Experiment Tracking Frontend

Frontend приложение для платформы отслеживания экспериментов, построенное на React + TypeScript + Vite.

## Возможности

- ✅ Просмотр списка экспериментов с фильтрацией
- ✅ Детальный просмотр эксперимента
- ✅ Создание новых экспериментов
- ✅ Просмотр запусков (runs) эксперимента
- ✅ Детальный просмотр запуска
- ✅ Управление статусами запусков
- ✅ Поиск экспериментов

## Технологии

- **React 18** - UI библиотека
- **TypeScript** - типизация
- **Vite** - сборщик и dev сервер
- **React Router** - роутинг
- **TanStack Query** - управление состоянием и кэширование запросов
- **Axios** - HTTP клиент
- **date-fns** - работа с датами

## Быстрый старт

### Установка зависимостей

```bash
npm install
```

### Запуск dev сервера

```bash
npm run dev
```

Приложение будет доступно на http://localhost:3000

### Запуск только фронта локально (бэкенд в Docker)

Если бэкенд и Auth Proxy уже подняты в Docker, можно запускать только портал на хосте — так удобнее разрабатывать UI с hot-reload.

1. **Поднять бэкенд и Auth Proxy (без контейнера портала):**

   Из корня репозитория:

   ```bash
   docker compose up -d postgres auth-service experiment-service telemetry-ingest-service auth-proxy
   ```

   Либо полный стек: `make dev` или `docker compose up -d`, тогда портал в Docker будет на 3000 — его можно остановить, чтобы освободить порт:

   ```bash
   docker compose stop experiment-portal
   ```

2. **Запустить портал локально:**

   ```bash
   cd projects/frontend/apps/experiment-portal
   npm ci
   npm run dev
   ```

3. Открыть в браузере: **http://localhost:3000**

Фронт по умолчанию ходит в **http://localhost:8080** (Auth Proxy). Если Auth Proxy на другом хосте/порту, создайте `.env` в каталоге `experiment-portal`:

```env
VITE_AUTH_PROXY_URL=http://localhost:8080
```

### Сборка для production

```bash
npm run build
```

Собранные файлы будут в папке `dist/`

## Конфигурация

### Переменные окружения

Создайте файл `.env` (опционально):

```env
# Auth Proxy (BFF). По умолчанию: http://localhost:8080
VITE_AUTH_PROXY_URL=http://localhost:8080

# Override (опционально): прямой Telemetry Ingest Service.
# Если не задано — телеметрия (REST ingest + SSE stream) идёт через auth-proxy.
# VITE_TELEMETRY_INGEST_URL=http://localhost:8003
```

По умолчанию frontend ходит в backend через **Auth Proxy**:
- `/api/*` → auth-proxy → experiment-service
- `/projects/*` → auth-proxy → auth-service
- `/api/v1/telemetry/*` → auth-proxy → telemetry-ingest-service (с `Authorization: Bearer <sensor_token>`)

### Проксирование запросов

Для разработки настроен прокси в `vite.config.ts`:
- Запросы к `/api/*` и `/projects/*` проксируются на `VITE_AUTH_PROXY_URL` (по умолчанию `http://localhost:8080`)

## Структура проекта

```
experiment-portal/
├── src/
│   ├── api/           # API клиент
│   ├── components/     # Переиспользуемые компоненты
│   ├── pages/         # Страницы приложения
│   ├── types/         # TypeScript типы
│   ├── App.tsx        # Корневой компонент
│   └── main.tsx       # Точка входа
├── public/            # Статические файлы
├── index.html
├── vite.config.ts
└── package.json
```

## Страницы

### `/` или `/experiments`
Список экспериментов с фильтрацией и поиском

### `/experiments/new`
Создание нового эксперимента

### `/experiments/:id`
Детальный просмотр эксперимента и его запусков

### `/runs/:id`
Детальный просмотр запуска

## API Интеграция

Frontend ходит в backend **через Auth Proxy** (BFF):

- `/api/*` → auth-proxy → experiment-service
- `/projects/*` → auth-proxy → auth-service
- `/api/v1/telemetry/*` → auth-proxy → telemetry-ingest-service

Ключевые ручки Experiment Service (v1):

- `GET /api/v1/experiments` — список экспериментов (пагинация)
- `GET /api/v1/experiments/search` — поиск экспериментов
- `POST /api/v1/experiments` — создание эксперимента
- `GET /api/v1/experiments/:id` — детали эксперимента
- `PATCH /api/v1/experiments/:id` — обновление эксперимента
- `POST /api/v1/experiments/:id/archive` — архивирование
- `DELETE /api/v1/experiments/:id` — удаление
- `GET /api/v1/experiments/:id/runs` — список запусков
- `POST /api/v1/experiments/:id/runs` — создание запуска
- `GET /api/v1/runs/:id` — детали запуска
- `PATCH /api/v1/runs/:id` — обновление запуска (в т.ч. смена статуса)
- `POST /api/v1/runs:batch-status` — массовая смена статуса
- `POST /api/v1/runs:bulk-tags` — bulk tagging
- `GET /api/v1/runs/:id/capture-sessions` — список capture sessions
- `POST /api/v1/runs/:id/capture-sessions` — старт capture session
- `POST /api/v1/runs/:id/capture-sessions/:cs_id/stop` — стоп capture session
- `GET /api/v1/sensors` — список датчиков
- `POST /api/v1/sensors` — создание/регистрация датчика
- `POST /api/v1/sensors/:id/rotate-token` — ротация токена

## Аутентификация

Аутентификация реализована через **Auth Proxy** и **HttpOnly cookies**:

- Frontend делает `POST /auth/login` на auth-proxy (cookies выставляются прокси).
- Axios настроен с `withCredentials: true`, поэтому cookies автоматически уходят на auth-proxy.
- При `401` работает auto-refresh через `POST /auth/refresh`; если refresh не удался — редирект на `/login`.

## CSRF

Auth Proxy использует защиту **double-submit cookie**:
- после `POST /auth/login` или `POST /auth/refresh` прокси выставляет cookie `csrf_token`
- frontend автоматически добавляет заголовок `X-CSRF-Token` (значение из cookie) для POST/PUT/PATCH/DELETE запросов

## Debugging (коротко)

- Все запросы содержат `X-Trace-Id` и `X-Request-Id`.
- При ошибках запросов в dev-режиме показывается debug toast снизу справа с кнопками **Details** и **Copy** (sanitized request/response + correlation ids).
- По `trace_id`/`request_id` удобно искать логи в Grafana/Loki (см. `docs/logging-flow.md` и `docs/grafana-trace-filtering.md`).

## Разработка

### Добавление новой страницы

1. Создайте компонент в `src/pages/`
2. Добавьте роут в `src/App.tsx`
3. При необходимости добавьте API методы в `src/api/client.ts`

### Стилизация

Используются CSS модули и глобальные стили в `src/App.css`. Компоненты имеют свои CSS файлы.

## Production Deployment

После сборки (`npm run build`), файлы из `dist/` можно разместить на любом статическом хостинге:
- Nginx
- Apache
- Vercel
- Netlify
- GitHub Pages

