# PRD: Мобильное приложение Bartini Labs (Android)

> **Версия документа:** 0.1.0
> **Дата:** 2026-03-07
> **Статус:** Draft
> **Автор:** Команда Bartini Labs

---

## 1. Введение и цели

### 1.1 Назначение документа

Описать требования к мобильному Android-приложению для платформы Bartini Labs Experiment Tracking Platform. Приложение предоставляет **read-only дашборд** для мониторинга экспериментов, запусков, сессий захвата и телеметрии с датчиков.

### 1.2 Контекст

Платформа Bartini Labs уже включает:
- Auth Service (JWT, RBAC: owner/editor/viewer)
- Experiment Service (эксперименты, запуски, capture sessions, датчики)
- Telemetry Ingest Service (REST ingest, SSE streaming, агрегированные запросы)
- Auth Proxy / BFF (Fastify, HttpOnly cookies, CSRF)
- Frontend SPA (React 19, TypeScript, Plotly)
- PostgreSQL 16 + TimescaleDB

Мобильное приложение (отмеченное как `❌ Мобильное приложение` в текущем ТЗ) расширяет охват платформы, позволяя оператору и аудитору наблюдать за экспериментами из любого места.

### 1.3 Цели продукта

| # | Цель | Метрика успеха |
|---|------|---------------|
| 1 | Live-мониторинг телеметрии в полевых условиях | Задержка отображения SSE-данных < 2 сек на LTE |
| 2 | Быстрый обзор статусов без открытия ноутбука | Время от запуска до просмотра списка экспериментов < 3 сек (кэш) |
| 3 | Push-уведомления о смене статуса | Уведомление приходит в течение 30 сек после события |

### 1.4 Вне scope

- Создание, редактирование, удаление сущностей (приложение read-only)
- Регистрация датчиков и ротация токенов
- Управление проектами и участниками
- Управление webhooks
- Экспорт CSV/JSON
- iOS-версия (возможна в будущем благодаря Flutter)

---

## 2. Целевая аудитория и персоны

### Персона 1: Оператор экспериментов

**Иван, 28 лет, инженер-испытатель**

- Запускает эксперименты через веб-интерфейс, но хочет следить за ходом из лаборатории, стоя у стенда
- Нужен live-график телеметрии на телефоне при проведении замеров
- Важно: получать push, если запуск упал (`failed`) или завершился (`succeeded`)
- Устройство: Android 12+, Samsung Galaxy S23

### Персона 2: Аудитор (Viewer)

**Мария, 35 лет, руководитель направления**

- Роль `viewer` в проекте, не создает данные, только просматривает
- Хочет утром на телефоне видеть статусы всех экспериментов за прошлые сутки
- Просматривает исторические графики телеметрии
- Устройство: Android 13+, Google Pixel 7

### Персона 3: Инженер устройств (Device Integrator)

**Алексей, 32 года, embedded-инженер**

- Следит за статусом датчиков (online/offline по last_heartbeat)
- Нужен быстрый доступ к списку сенсоров и их состоянию
- Проверяет поступление телеметрии в реальном времени
- Устройство: Android 11+, Xiaomi Redmi Note 12

---

## 3. User Stories

### Аутентификация

| ID | Роль | User Story | Приоритет |
|----|------|------------|-----------|
| US-01 | Пользователь | Как пользователь, я хочу войти с логином/паролем, чтобы получить доступ к своим проектам | P0 |
| US-02 | Пользователь | Как пользователь, я хочу оставаться в системе до 14 дней (refresh token), чтобы не вводить пароль каждый день | P0 |
| US-03 | Пользователь | Как пользователь, я хочу выйти из аккаунта и стереть токены с устройства | P0 |

### Проекты и эксперименты

| ID | Роль | User Story | Приоритет |
|----|------|------------|-----------|
| US-04 | Viewer/Editor/Owner | Как участник проекта, я хочу видеть список проектов, в которых я состою | P0 |
| US-05 | Viewer/Editor/Owner | Как участник, я хочу видеть список экспериментов текущего проекта с фильтрацией по статусу | P0 |
| US-06 | Viewer/Editor/Owner | Как участник, я хочу найти эксперимент по названию/описанию | P0 |
| US-07 | Viewer/Editor/Owner | Как участник, я хочу открыть карточку эксперимента и увидеть его запуски | P0 |

### Запуски и сессии захвата

| ID | Роль | User Story | Приоритет |
|----|------|------------|-----------|
| US-08 | Viewer/Editor/Owner | Как участник, я хочу видеть детали запуска: статус, параметры, git_sha, длительность | P0 |
| US-09 | Viewer/Editor/Owner | Как участник, я хочу видеть список capture sessions запуска с номером, статусом и временем | P0 |
| US-10 | Viewer/Editor/Owner | Как участник, я хочу фильтровать запуски по статусу | P1 |

### Телеметрия

| ID | Роль | User Story | Приоритет |
|----|------|------------|-----------|
| US-11 | Editor/Owner | Как оператор, я хочу видеть live-график телеметрии (SSE) при активном запуске | P0 |
| US-12 | Viewer/Editor/Owner | Как участник, я хочу просматривать историческую телеметрию capture session с агрегацией 1m | P0 |
| US-13 | Viewer/Editor/Owner | Как участник, я хочу переключать raw/physical значения на графике | P1 |
| US-14 | Viewer/Editor/Owner | Как участник, я хочу видеть несколько сенсоров на одном графике | P1 |

### Датчики

| ID | Роль | User Story | Приоритет |
|----|------|------------|-----------|
| US-15 | Viewer/Editor/Owner | Как участник, я хочу видеть список датчиков проекта с индикатором статуса | P0 |
| US-16 | Viewer/Editor/Owner | Как участник, я хочу видеть last_heartbeat каждого датчика | P1 |

### Уведомления

| ID | Роль | User Story | Приоритет |
|----|------|------------|-----------|
| US-17 | Viewer/Editor/Owner | Как участник, я хочу получать push при изменении статуса запуска (running -> succeeded/failed) | P1 |
| US-18 | Viewer/Editor/Owner | Как участник, я хочу настроить, для каких проектов получать уведомления | P2 |

### Оффлайн

| ID | Роль | User Story | Приоритет |
|----|------|------------|-----------|
| US-19 | Viewer/Editor/Owner | Как участник, я хочу видеть кэшированный список экспериментов при отсутствии сети | P1 |
| US-20 | Viewer/Editor/Owner | Как участник, я хочу видеть индикатор отсутствия связи и время последнего обновления | P1 |

---

## 4. Функциональные требования (по экранам)

### 4.1 Экран входа (Login)

```
+----------------------------------+
|                                  |
|        [Bartini Labs logo]       |
|     Experiment Tracking          |
|                                  |
|  +----------------------------+  |
|  | Имя пользователя           |  |
|  +----------------------------+  |
|                                  |
|  +----------------------------+  |
|  | Пароль                [eye]|  |
|  +----------------------------+  |
|                                  |
|  +----------------------------+  |
|  | Адрес сервера              |  |
|  +----------------------------+  |
|                                  |
|  [========= Войти =========]    |
|                                  |
|  Запомнить адрес сервера [x]     |
|                                  |
+----------------------------------+
```

**Требования:**
- Поля: username, password, server URL (base URL Auth Service)
- Валидация: все поля обязательны, URL начинается с `http://` или `https://`
- Сохранение server URL в SharedPreferences (незащищённое хранилище)
- Хранение JWT (access_token, refresh_token) в `flutter_secure_storage` (Android Keystore)
- Ошибки: "Неверные учётные данные", "Сервер недоступен", "Неверный формат URL"
- Кнопка "Показать/скрыть пароль"

### 4.2 Экран списка проектов (Projects)

```
+----------------------------------+
| Проекты                [avatar] |
+----------------------------------+
| +------------------------------+ |
| | Проект Alpha                 | |
| | 5 экспериментов  owner       | |
| +------------------------------+ |
| +------------------------------+ |
| | Проект Beta                  | |
| | 12 экспериментов  editor     | |
| +------------------------------+ |
| +------------------------------+ |
| | Проект Gamma                 | |
| | 3 эксперимента   viewer      | |
| +------------------------------+ |
|                                  |
+----------------------------------+
| [Projects] [Sensors] [Settings] |
+----------------------------------+
```

**Требования:**
- Список проектов из `GET /projects` (Auth Service) с ролью пользователя
- Pull-to-refresh
- Кэширование списка для оффлайн-доступа
- Нажатие переходит к списку экспериментов проекта

### 4.3 Экран списка экспериментов (Experiments)

```
+----------------------------------+
| [<] Проект Alpha        [search]|
+----------------------------------+
| [All] [Running] [Succeeded] ... |
+----------------------------------+
| +------------------------------+ |
| | Эксперимент #1        [RUN]  | |
| | Calibration test             | |
| | 3 запуска  tags: v2,gps      | |
| | 2026-03-06 14:32             | |
| +------------------------------+ |
| +------------------------------+ |
| | Эксперимент #2        [DONE] | |
| | Baseline measurement         | |
| | 1 запуск   tags: baseline    | |
| | 2026-03-05 09:15             | |
| +------------------------------+ |
| ... (infinite scroll)           |
+----------------------------------+
| [Projects] [Sensors] [Settings] |
+----------------------------------+
```

**Требования:**
- `GET /api/v1/experiments?project_id=...&status=...&page=...&page_size=20`
- Фильтр-чипы по статусу: All, Draft, Running, Succeeded, Failed, Archived
- Поиск: `GET /api/v1/experiments/search?project_id=...&q=...`
- Бесконечная прокрутка (cursor-based пагинация)
- Цветной бейдж статуса (Material 3)
- Отображение тегов (chips)
- Pull-to-refresh

### 4.4 Экран деталей эксперимента (Experiment Detail)

```
+----------------------------------+
| [<] Эксперимент #1      [share]|
+----------------------------------+
| Calibration test                 |
| Статус: [RUNNING]                |
| Теги: [v2] [gps] [imu]          |
| Описание: Калибровка датчиков... |
| Создан: 2026-03-06 14:32        |
+----------------------------------+
| Запуски (3)                      |
+----------------------------------+
| +------------------------------+ |
| | Run #1          [RUNNING]    | |
| | git: a1b2c3d   2m 34s       | |
| | params: lr=0.01, batch=32   | |
| +------------------------------+ |
| +------------------------------+ |
| | Run #2          [SUCCEEDED]  | |
| | git: e4f5g6h   15m 22s      | |
| +------------------------------+ |
+----------------------------------+
```

**Требования:**
- `GET /api/v1/experiments/{id}` -- детали эксперимента
- `GET /api/v1/experiments/{id}/runs?status=...` -- список запусков
- Секция метаданных: name, description, status badge, tags, created_at, updated_at
- Список запусков с фильтрацией по статусу
- Каждый run показывает: статус, git_sha (сокращённый), duration, ключевые params

### 4.5 Экран деталей запуска (Run Detail)

```
+----------------------------------+
| [<] Run #1                       |
+----------------------------------+
| Статус: [RUNNING]                |
| Начат: 2026-03-06 14:35         |
| Длительность: 2m 34s (live)     |
| Git SHA: a1b2c3d4e5f6           |
| Окружение: production           |
+----------------------------------+
| Параметры                    [v] |
|   lr: 0.01                       |
|   batch_size: 32                 |
|   epochs: 100                    |
+----------------------------------+
| Capture Sessions (2)             |
+----------------------------------+
| | #1  [SUCCEEDED]  00:32-01:15 | |
| | #2  [RUNNING]    02:10-...   | |
+----------------------------------+
| [== Открыть телеметрию ==]       |
+----------------------------------+
```

**Требования:**
- `GET /api/v1/runs/{id}` -- детали запуска
- `GET /api/v1/runs/{id}/capture-sessions` -- список capture sessions
- Секции: статус, временные метки, git_sha, env, params (развернуть/свернуть), notes
- Список capture sessions: ordinal_number, status badge, started_at, stopped_at
- Кнопка "Открыть телеметрию" переходит к графикам

### 4.6 Экран телеметрии (Telemetry Viewer)

```
+----------------------------------+
| [<] Телеметрия  [live|history]   |
+----------------------------------+
| Сессия: [#1 v] Сенсор: [All v]  |
+----------------------------------+
|                                  |
| ~~~/\  /\  /~~~\/\  /\  ~~~~    |
|      \/  \/        \/  \/       |
|                                  |
| raw | physical      zoom: [+-]  |
+----------------------------------+
|                                  |
| ___/-----\____/-------\___      |
|                                  |
| Sensor: IMU-X   Sensor: IMU-Y   |
+----------------------------------+
| Последнее обновление: 14:37:22  |
+----------------------------------+
```

**Требования:**

**Режим Live (SSE):**
- `GET /api/v1/telemetry/stream?sensor_id=...&access_token=...`
- SSE-подключение через EventSource-совместимый клиент
- Токен передается в query parameter `access_token` (SSE не поддерживает кастомные заголовки)
- Скользящее окно: последние 500 точек на графике
- Автопрокрутка вправо
- Индикатор статуса подключения (connected/reconnecting/disconnected)

**Режим History:**
- `GET /api/v1/telemetry/aggregated?capture_session_id=...&sensor_id=...&time_from=...&time_to=...&limit=5000`
- Выбор capture session из dropdown
- Фильтр по sensor_id (multi-select)
- Отображение avg_raw/avg_physical (переключатель)
- Минимумы/максимумы как область на графике
- Pinch-to-zoom и горизонтальный скролл по оси времени

**Общее:**
- Переключатель raw/physical
- Несколько сенсоров на одном или отдельных графиках
- Ландшафтная ориентация для полноэкранного просмотра

### 4.7 Экран датчиков (Sensors)

```
+----------------------------------+
| Датчики                [filter] |
+----------------------------------+
| +------------------------------+ |
| | [*] IMU-Accelerometer        | |
| |     Тип: accelerometer       | |
| |     Единица: m/s^2           | |
| |     Heartbeat: 2 сек назад   | |
| +------------------------------+ |
| +------------------------------+ |
| | [!] GPS-Module               | |
| |     Тип: gps                  | |
| |     Единица: deg              | |
| |     Heartbeat: 5 мин назад   | |
| +------------------------------+ |
| +------------------------------+ |
| | [x] Temp-Sensor-01           | |
| |     Тип: temperature          | |
| |     Единица: C                | |
| |     Heartbeat: 2 часа назад  | |
| +------------------------------+ |
+----------------------------------+
| [Projects] [Sensors] [Settings] |
+----------------------------------+
```

**Требования:**
- `GET /api/v1/sensors?project_id=...`
- Индикатор состояния по `last_heartbeat`:
  - Зелёный (`*`): heartbeat < 30 сек назад (online)
  - Жёлтый (`!`): heartbeat 30 сек - 5 мин назад (delayed)
  - Красный (`x`): heartbeat > 5 мин назад или null (offline)
- Отображение: name, type, input_unit, display_unit, relative time of last_heartbeat
- Pull-to-refresh
- Фильтр по типу сенсора

### 4.8 Экран настроек (Settings)

```
+----------------------------------+
| Настройки                        |
+----------------------------------+
| Профиль                         |
|   user: ivan.petrov              |
|   Сервер: https://lab.example   |
+----------------------------------+
| Уведомления                     |
|   Push-уведомления    [toggle]   |
|   Проекты для push:              |
|     [x] Проект Alpha             |
|     [ ] Проект Beta              |
+----------------------------------+
| Кэш                             |
|   Размер кэша: 12 MB            |
|   [Очистить кэш]                |
+----------------------------------+
| Тема                             |
|   [Auto] [Light] [Dark]         |
+----------------------------------+
| [======= Выйти =======]         |
|                                  |
| Версия: 0.1.0                   |
+----------------------------------+
| [Projects] [Sensors] [Settings] |
+----------------------------------+
```

**Требования:**
- Профиль: данные из `GET /auth/me` (Auth Service)
- Push-уведомления: toggle on/off, выбор проектов
- Кэш: показ размера, очистка
- Тема: Auto (системная), Light, Dark
- Кнопка "Выйти": очистка tokens из secure storage, переход на экран логина

---

## 5. Информационная архитектура (навигация)

```
Login
  |
  v
BottomNavigation
  |
  +-- Projects (Tab 1)
  |     |
  |     +-- Experiments List
  |           |
  |           +-- Experiment Detail
  |                 |
  |                 +-- Run Detail
  |                       |
  |                       +-- Telemetry Viewer (Live / History)
  |
  +-- Sensors (Tab 2)
  |
  +-- Settings (Tab 3)
```

**Навигация:**
- `BottomNavigationBar` с 3 вкладками: Проекты, Датчики, Настройки
- Внутри вкладки "Проекты" -- иерархический drill-down с `go_router`
- Глубокие ссылки (deep links): `bartinilabs://experiments/{id}`, `bartinilabs://runs/{id}`
- Back button: стандартный Android back stack

---

## 6. Технический стек и архитектура

### 6.1 Стек

| Слой | Технология | Версия |
|------|-----------|--------|
| Фреймворк | Flutter | 3.27+ |
| Язык | Dart | 3.6+ |
| Минимальный Android SDK | 24 (Android 7.0) | -- |
| Target Android SDK | 35 (Android 15) | -- |
| State Management | Riverpod (`riverpod` + `flutter_riverpod` + `riverpod_annotation`) | 2.x |
| Навигация | go_router | 14.x |
| HTTP-клиент | dio | 5.x |
| SSE-клиент | dio + пользовательский StreamTransformer (или пакет `eventsource_client`) | -- |
| Графики | fl_chart | 0.70+ |
| Безопасное хранение | flutter_secure_storage | 9.x |
| Локальный кэш | drift (SQLite) | 2.x |
| Push-уведомления | firebase_messaging | 15.x |
| Сериализация | freezed + json_serializable | 2.x / 6.x |
| Code Generation | build_runner | -- |
| Тема | Material 3 (`dynamic_color`) | -- |
| Connectivity | connectivity_plus | 6.x |
| Линтинг | very_good_analysis | 6.x |
| Тесты | flutter_test, mockito, integration_test | -- |

### 6.2 Архитектура приложения

Четырёхслойная архитектура (Clean Architecture lite):

```
+-----------------------------------------+
|             Presentation                |
|  (Screens, Widgets, Riverpod Providers) |
+-----------------------------------------+
|             Application                 |
|     (Use Cases / Service classes)       |
+-----------------------------------------+
|               Domain                    |
|    (Entities, Repository interfaces)    |
+-----------------------------------------+
|            Infrastructure               |
|  (API clients, Drift DB, Secure Store)  |
+-----------------------------------------+
```

**Структура проекта:**

```
projects/mobile/
+-- android/
+-- lib/
|   +-- main.dart
|   +-- app.dart
|   +-- core/
|   |   +-- config/           # Конфигурация (base URL, timeouts)
|   |   +-- network/          # Dio setup, interceptors, auth interceptor
|   |   +-- storage/          # flutter_secure_storage wrapper
|   |   +-- theme/            # Material 3 theme (Bartini Labs palette)
|   |   +-- router/           # go_router config
|   +-- domain/
|   |   +-- models/           # freezed: Experiment, Run, Sensor, etc.
|   |   +-- repositories/     # abstract classes
|   +-- infrastructure/
|   |   +-- api/              # Dio clients per service
|   |   +-- db/               # Drift database (cache)
|   |   +-- repositories/     # Impl of domain repos
|   +-- application/
|   |   +-- providers/        # Riverpod providers
|   +-- presentation/
|       +-- auth/
|       +-- projects/
|       +-- experiments/
|       +-- runs/
|       +-- telemetry/
|       +-- sensors/
|       +-- settings/
|       +-- shared/           # Common widgets (badges, charts, etc.)
+-- test/
|   +-- unit/
|   +-- widget/
|   +-- integration/
+-- pubspec.yaml
+-- analysis_options.yaml
```

### 6.3 Генерация моделей из OpenAPI

Dart-модели генерируются из существующей OpenAPI спецификации (`projects/backend/services/experiment-service/openapi/openapi.yaml`) с помощью `openapi_generator_cli` или `swagger_parser`. Это обеспечивает синхронизацию типов с бэкендом, аналогично тому, как TypeScript SDK генерируется через `make generate-sdk`.

Добавить в Makefile:

```makefile
generate-mobile-models:  ## Dart models from OpenAPI
	dart run openapi_generator_cli generate \
		-i projects/backend/services/experiment-service/openapi/openapi.yaml \
		-g dart-dio \
		-o projects/mobile/lib/infrastructure/api/generated
```

---

## 7. API-интеграция и аутентификация

### 7.1 Подход к аутентификации на мобильном

Веб-SPA использует Auth Proxy (BFF на Fastify), который хранит JWT в HttpOnly cookies и добавляет CSRF-защиту. Мобильное приложение **не использует BFF**, а работает напрямую с Auth Service (порт 8001), поскольку:

1. Auth Service уже возвращает `access_token` и `refresh_token` в JSON response body при `POST /auth/login`
2. Мобильное приложение не работает с cookies -- оно хранит токены в Android Keystore через `flutter_secure_storage`
3. CSRF не нужен -- приложение не использует cookies, а Bearer-токен не подвержен CSRF

### 7.2 Схема аутентификации

```
+-----------+     POST /auth/login      +---------------+
|  Mobile   | ------------------------> | Auth Service   |
|   App     | <------------------------ |  (port 8001)   |
|           |  { access_token,          +---------------+
|           |    refresh_token }
|           |
|           |     GET /api/v1/...       +---------------+
|           | ------------------------> |  Experiment    |
|           |   Authorization: Bearer   |   Service      |
|           |                           |  (port 8002)   |
|           | <------------------------ +---------------+
|           |
|           |  GET /api/v1/telemetry/*  +---------------+
|           | ------------------------> |  Telemetry     |
|           |   Authorization: Bearer   |   Ingest       |
|           |   (or ?access_token=)     |  (port 8003)   |
+-----------+ <------------------------ +---------------+
```

### 7.3 Auth Flow

1. **Login**: `POST {AUTH_URL}/auth/login` с `{ username, password }` -> получаем `{ access_token, refresh_token }`
2. **Хранение**: оба токена сохраняются в `flutter_secure_storage` (Android Keystore)
3. **Запросы**: Dio interceptor добавляет `Authorization: Bearer {access_token}` к каждому запросу
4. **Refresh**: при 401 Dio interceptor автоматически вызывает `POST {AUTH_URL}/auth/refresh` с `{ refresh_token }`, обновляет токены и retry запрос
5. **SSE**: для SSE-потока (EventSource) токен передаётся в query parameter `access_token=...` (SSE API уже поддерживает это)
6. **Logout**: `POST {AUTH_URL}/auth/logout` + очистка secure storage
7. **Expired refresh**: если refresh тоже истёк, переход на экран логина

### 7.4 Dio Interceptor (псевдокод)

```dart
class AuthInterceptor extends QueuedInterceptor {
  // onRequest: добавить Bearer token
  // onError (401): попробовать refresh, при успехе -- retry, при неудаче -- logout
}
```

### 7.5 Необходимые изменения бэкенда

Для полноценной работы мобильного приложения необходимы минимальные изменения:

1. **Рекомендуемый подход**: добавить в Auth Proxy эндпоинт `POST /auth/mobile/login`, который возвращает токены в теле ответа (без cookies), а все API-запросы по-прежнему проксировать через Auth Proxy. Это сохраняет единую точку авторизации.
2. **Альтернатива**: использовать Auth Service напрямую (порт 8001), который уже возвращает JWT в теле ответа, и направлять API-запросы напрямую к Experiment Service / Telemetry Ingest Service с Bearer-токеном.
3. **Push-уведомления**: добавить таблицу `device_tokens` в auth_db и endpoint `POST /auth/devices` для регистрации FCM-токенов. При webhook-событии отправлять push через Firebase Admin SDK.

### 7.6 Маппинг API-эндпоинтов

| Экран | Эндпоинт | Сервис |
|-------|---------|--------|
| Login | `POST /auth/login` | Auth Service |
| Refresh | `POST /auth/refresh` | Auth Service |
| Profile | `GET /auth/me` | Auth Service |
| Projects | `GET /projects` | Auth Service (через Auth Proxy) |
| Experiments list | `GET /api/v1/experiments?project_id=&status=&page=&page_size=` | Experiment Service |
| Experiment search | `GET /api/v1/experiments/search?project_id=&q=` | Experiment Service |
| Experiment detail | `GET /api/v1/experiments/{id}` | Experiment Service |
| Runs list | `GET /api/v1/experiments/{id}/runs` | Experiment Service |
| Run detail | `GET /api/v1/runs/{id}` | Experiment Service |
| Capture sessions | `GET /api/v1/runs/{id}/capture-sessions` | Experiment Service |
| Sensors list | `GET /api/v1/sensors?project_id=` | Experiment Service |
| Live telemetry | `GET /api/v1/telemetry/stream?sensor_id=&access_token=` | Telemetry Ingest |
| History telemetry | `GET /api/v1/telemetry/aggregated?capture_session_id=&sensor_id=` | Telemetry Ingest |

---

## 8. Дизайн

### 8.1 Визуальный язык

Приложение следует дизайн-системе Bartini Labs, основанной на Material Design 3 с purple baseline palette (совпадает с веб-версией).

**Основные цвета (из текущей дизайн-системы `colors.scss`):**

| Токен | Значение | Применение |
|-------|---------|-----------|
| Primary | `#6750A4` | Кнопки, active state, ссылки |
| Primary Container | `#EADDFF` | Chip, selected state background |
| Secondary | `#625B71` | Secondary text, icons |
| Surface | `#FFFBFE` | Фон карточек |
| Background | `#FFFBFE` | Фон экранов |
| Error | `#B3261E` | Failed badge, ошибки |
| Success | `#386A20` | Succeeded badge |
| Warning | `#7C4E00` | Delayed sensor |
| Outline | `#CAC4D0` | Borders |
| Surface Variant | `#E7E0EC` | Secondary surfaces |
| Text | `#1C1B1F` | Primary text |
| Text Secondary | `#49454F` | Muted text |

**Типографика:**
- Заголовки: системный sans-serif
- Моноширинный (git_sha, params): `RobotoMono`

### 8.2 Бейджи статусов

Цветные Pill-бейджи, аналогичные веб-компонентам:

| Статус | Цвет фона | Цвет текста |
|--------|----------|------------|
| draft | `rgba(98, 91, 113, 0.12)` | Secondary |
| running | `rgba(103, 80, 164, 0.12)` | Primary |
| succeeded | `rgba(56, 106, 32, 0.12)` | Success |
| failed | `rgba(179, 38, 30, 0.12)` | Error |
| archived | `rgba(28, 27, 31, 0.08)` | Muted |

### 8.3 Dark Theme

Material 3 автоматически генерирует dark palette из seed color `#6750A4` через `ColorScheme.fromSeed()`.

### 8.4 Графики

- `fl_chart` с кастомной палитрой (primary, secondary, tertiary цвета из M3 palette)
- Touch-интерактивность: показ значения в tooltip при нажатии
- Линии сетки: `outline` цвет с opacity 0.12

---

## 9. Нефункциональные требования

### 9.1 Производительность

| Метрика | Целевое значение |
|---------|-----------------|
| Cold start | < 2 сек (до отрисовки первого экрана) |
| Загрузка списка экспериментов | < 1.5 сек (сеть LTE), мгновенно (кэш) |
| Рендер графика (500 точек) | < 300 мс |
| SSE reconnect | < 3 сек |
| Memory footprint | < 150 MB в активном состоянии |
| APK size | < 25 MB (AAB < 15 MB) |

### 9.2 Безопасность

| Требование | Реализация |
|-----------|-----------|
| Хранение JWT | `flutter_secure_storage` -> Android Keystore (hardware-backed) |
| Сертификаты | Certificate pinning через `dio` `SecurityContext` (для production) |
| Root detection | Опционально, warn-only (не блокировать) |
| Screen capture | Не блокировать (read-only данные) |
| Token expiry | access_token TTL = 900 сек, refresh_token TTL = 14 дней |
| Logout | Полная очистка secure storage + кэша |
| ProGuard/R8 | Включить обфускацию для release build |

### 9.3 Оффлайн-поддержка

**Стратегия: Cache-first с network refresh.**

| Данные | Кэш | TTL кэша |
|--------|-----|---------|
| Projects list | Drift (SQLite) | 1 час |
| Experiments list | Drift (SQLite) | 15 мин |
| Experiment detail | Drift (SQLite) | 15 мин |
| Run detail | Drift (SQLite) | 15 мин |
| Sensors list | Drift (SQLite) | 5 мин |
| Capture sessions | Drift (SQLite) | 15 мин |
| Historical telemetry | Drift (SQLite) | 1 час (per capture_session_id) |
| Live telemetry | Не кэшируется (real-time) | -- |

**При отсутствии сети:**
- Показывать кэшированные данные с баннером "Нет соединения. Данные от {timestamp}"
- Live-телеметрия недоступна, показать заглушку
- Pull-to-refresh показывает SnackBar "Нет сети"

**Drift (SQLite) схема:**
- Таблицы-зеркала для каждой сущности (projects, experiments, runs, sensors, capture_sessions)
- Таблица `telemetry_cache` с индексом по (capture_session_id, sensor_id)
- Таблица `cache_metadata` с last_synced_at для каждого cache key

### 9.4 Accessibility

- Минимальный контраст текста: 4.5:1 (WCAG AA)
- Semantic labels для screen readers (TalkBack)
- Минимальный размер touch target: 48x48dp

### 9.5 Тестирование

| Уровень | Инструмент | Покрытие |
|---------|-----------|---------|
| Unit | flutter_test + mockito | Providers, models, interceptors |
| Widget | flutter_test | Каждый экран с mock data |
| Integration | integration_test | Login flow, drill-down эксперимент -> run -> телеметрия |
| Golden | alchemist | Key screens (light/dark) |

Целевое покрытие: > 70% (unit + widget).

---

## 10. План релизов

### Phase 1: MVP (4 недели)

**Цель:** Базовый read-only доступ к экспериментам.

| Неделя | Задачи |
|--------|--------|
| 1 | Scaffolding проекта, Dio + Auth interceptor, Login экран, secure storage |
| 2 | Projects list, Experiments list (с фильтрами), Experiment detail |
| 3 | Run detail, Capture sessions list, Sensors list |
| 4 | Тестирование, bug fixes, internal alpha release |

**Критерии выхода MVP:**
- US-01..US-09, US-15 реализованы
- Приложение работает на Android 11+
- Unit-тесты > 50%
- APK < 25 MB

### Phase 2: v1.0 -- Live Telemetry (3 недели)

| Неделя | Задачи |
|--------|--------|
| 5 | SSE-клиент, Live telemetry экран (fl_chart + real-time) |
| 6 | History telemetry (aggregated API), raw/physical переключатель, multi-sensor |
| 7 | Offline кэш (Drift), connectivity indicator, dark theme, тесты |

**Критерии выхода v1.0:**
- US-11..US-14, US-19..US-20 реализованы
- Live SSE работает стабильно (auto-reconnect)
- Оффлайн-доступ к кэшированным данным
- Unit + widget тесты > 60%

### Phase 3: v1.1 -- Notifications & Polish (3 недели)

| Неделя | Задачи |
|--------|--------|
| 8 | Firebase Cloud Messaging интеграция, backend webhook -> FCM bridge |
| 9 | Push notification settings UI, deep links, landscape telemetry |
| 10 | Performance optimization, accessibility audit, release candidate |

**Критерии выхода v1.1:**
- US-17..US-18 реализованы
- Push-уведомления работают end-to-end
- Deep links: `bartinilabs://experiments/{id}` открывает экран эксперимента
- Integration tests для основных flows
- Публикация в Google Play Internal Testing

---

## 11. Критерии приёмки

| # | Критерий | Проверка |
|---|---------|---------|
| 1 | Пользователь может войти с логином/паролем и увидеть список проектов | Ручное + integration test |
| 2 | Список экспериментов загружается с фильтрацией по статусу и поиском | Widget test + ручное |
| 3 | Drill-down: Проект -> Эксперимент -> Run -> Capture Sessions отображается корректно | Integration test |
| 4 | Live-телеметрия отображается в реальном времени (SSE) с задержкой < 2 сек | Ручное с sensor-simulator |
| 5 | Историческая телеметрия отображается с агрегацией 1m, zoom и pan работают | Ручное |
| 6 | Список датчиков показывает корректные индикаторы online/delayed/offline | Unit test (heartbeat logic) + ручное |
| 7 | При отсутствии сети показываются кэшированные данные с баннером | Airplane mode test |
| 8 | Токены хранятся в Android Keystore, при logout -- полностью очищаются | Security review |
| 9 | Приложение работает на Android 11, 12, 13, 14, 15 | Device farm |
| 10 | APK < 25 MB, cold start < 2 сек | Benchmark |
| 11 | Push-уведомления доставляются при смене статуса запуска (v1.1) | E2E manual test |
| 12 | Dark theme корректно отображает все экраны | Golden tests |

---

## 12. Риски и ограничения

### Риски

| # | Риск | Вероятность | Импакт | Митигация |
|---|------|-----------|--------|-----------|
| 1 | Auth Service недоступен напрямую из внешней сети (только через Docker network) | Высокая | Высокий | Добавить мобильный auth endpoint в Auth Proxy (`POST /auth/mobile/login`) или развернуть Auth Service с внешним портом |
| 2 | SSE нестабильна на мобильной сети (LTE/3G) | Средняя | Средний | Exponential backoff reconnect; показывать timestamp последнего события; fallback на polling каждые 5 сек |
| 3 | Высокое потребление батареи при длительном SSE | Средняя | Средний | Auto-disconnect SSE при сворачивании приложения; опция "Keep alive in background" (по умолчанию off) |
| 4 | OpenAPI spec не покрывает все эндпоинты (telemetry aggregated, projects) | Высокая | Низкий | Для непокрытых эндпоинтов писать модели вручную; дополнить OpenAPI спецификацию |
| 5 | Большие объёмы телеметрии (20k точек) на слабых устройствах | Низкая | Средний | Ограничить `limit=2000` для мобильного; использовать aggregated API; progressive loading |
| 6 | Push notifications требуют изменений бэкенда (FCM bridge) | Высокая | Средний | Реализовать в Phase 3; webhook subscriber -> Firebase Admin SDK; при невозможности -- отказаться от push в v1.0 |
| 7 | flutter_secure_storage на некоторых Android-устройствах использует EncryptedSharedPreferences вместо Keystore | Низкая | Средний | Проверить `canAuthenticate` API; документировать поддерживаемые устройства |
| 8 | CORS при прямом обращении к сервисам | Нет | Нет | Мобильное приложение не использует CORS (это браузерный механизм); нативный HTTP-клиент работает без ограничений |

### Ограничения

1. **Read-only**: приложение не поддерживает создание/редактирование сущностей. Для write-операций используйте веб-интерфейс.
2. **Android only**: iOS потенциально возможен (Flutter кроссплатформенный), но не в scope данного PRD.
3. **Нет WebSocket**: бэкенд использует SSE, а не WebSocket. Мобильное приложение следует этому решению.
4. **Нет сравнения запусков**: Comparison Service не реализован на бэкенде (`❌`), поэтому не включён в мобильное приложение.
5. **Нет артефактов/метрик**: Artifact Service и Metrics Service не реализованы на бэкенде.
