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

Frontend использует Experiment Service API:

- `GET /experiments` - список экспериментов
- `POST /experiments` - создание эксперимента
- `GET /experiments/:id` - получение эксперимента
- `PUT /experiments/:id` - обновление эксперимента
- `DELETE /experiments/:id` - удаление эксперимента
- `GET /experiments/search` - поиск экспериментов
- `GET /experiments/:id/runs` - список запусков
- `POST /experiments/:id/runs` - создание запуска
- `GET /runs/:id` - получение запуска
- `PUT /runs/:id/complete` - завершение запуска
- `PUT /runs/:id/fail` - пометить как failed

## Аутентификация

Токен аутентификации хранится в `localStorage` под ключом `access_token` и автоматически добавляется в заголовки запросов.

При получении 401 ошибки происходит автоматический редирект на `/login` (нужно реализовать страницу входа).

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

