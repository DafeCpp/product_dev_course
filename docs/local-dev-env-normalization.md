# Нормализация окружения для локального запуска

Документ описывает процесс нормализации переменных окружения для единообразного запуска всех сервисов через Docker Compose.

## Проблемы текущей конфигурации

### 1. Разрозненные env.example файлы
- Каждый сервис имеет свой `env.example` с разными форматами
- Нет единого источника правды для всех переменных
- Дублирование переменных между сервисами

### 2. Несогласованность URL
- В локальной разработке используются `localhost`
- В Docker Compose нужно использовать имена сервисов
- Нет четкого разделения между локальным и Docker окружением

### 3. Отсутствие централизованного управления
- Нет единого `.env` файла в корне проекта
- Сложно управлять зависимостями между сервисами
- Нет документации по обязательным/опциональным переменным

## Решение

### Создан единый файл конфигурации

**Файл:** `env.docker.example` (в корне проекта)

Этот файл содержит:
- Все переменные для всех сервисов
- Разделение на Docker и локальные URL
- Группировку по сервисам
- Комментарии с пояснениями

### Структура переменных

#### Префиксы для Docker vs Local
- Без суффикса: для использования внутри Docker сети (имена сервисов)
- `_LOCAL`: для использования с хоста (localhost)

Пример:
```env
# Внутри Docker сети
DATABASE_URL=postgresql://postgres:postgres@postgres:5432/experiment_db

# С хоста (для локальной разработки)
DATABASE_URL_LOCAL=postgresql://postgres:postgres@localhost:5432/experiment_db
```

#### Группировка по сервисам
- `POSTGRES_*` - настройки PostgreSQL
- `EXPERIMENT_SERVICE_*` - настройки Experiment Service
- `AUTH_PROXY_*` - настройки Auth Proxy
- `VITE_*` - переменные для Vite (Experiment Portal)

## Использование

### Для Docker Compose

1. Скопируйте файл:
```bash
cp env.docker.example .env
```

2. При необходимости измените значения в `.env`

3. Docker Compose автоматически загрузит переменные из `.env`

### Для локальной разработки (без Docker)

Используйте переменные с суффиксом `_LOCAL` или настройте каждый сервис отдельно через его `env.example`.

## Маппинг переменных

### Experiment Service

| Переменная в env.docker.example | Переменная в experiment-service | Описание |
|--------------------------------|----------------------------------|----------|
| `DATABASE_URL` | `DATABASE_URL` | URL подключения к PostgreSQL |
| `EXPERIMENT_SERVICE_PORT` | `PORT` | Порт сервиса |
| `AUTH_SERVICE_URL` | `AUTH_SERVICE_URL` | URL Auth Service |
| `RABBITMQ_URL` | `RABBITMQ_URL` | URL RabbitMQ |
| `TELEMETRY_BROKER_URL` | `TELEMETRY_BROKER_URL` | URL Redis |

### Auth Proxy

| Переменная в env.docker.example | Переменная в auth-proxy | Описание |
|--------------------------------|-------------------------|----------|
| `AUTH_PROXY_PORT` | `PORT` | Порт прокси |
| `TARGET_EXPERIMENT_URL` | `TARGET_EXPERIMENT_URL` | URL Experiment Service |
| `AUTH_URL` | `AUTH_URL` | URL Auth Service |
| `CORS_ORIGINS` | `CORS_ORIGINS` | Разрешенные origins |

### Experiment Portal

| Переменная в env.docker.example | Использование | Описание |
|--------------------------------|---------------|----------|
| `VITE_API_URL` | Vite env variable | URL API для фронтенда |

## Следующие шаги

1. ✅ Создан `env.docker.example` с единой конфигурацией
2. ⏳ Обновить `docker-compose.yml` для использования переменных из `.env`
3. ⏳ Обновить Dockerfile'ы для поддержки переменных окружения
4. ⏳ Создать скрипты для автоматической генерации env файлов для каждого сервиса

## Рекомендации

### Обязательные переменные
- `POSTGRES_USER`, `POSTGRES_PASSWORD`, `POSTGRES_DB`
- `DATABASE_URL`
- `EXPERIMENT_SERVICE_PORT`
- `AUTH_PROXY_PORT`

### Опциональные переменные
- `RABBITMQ_URL` - не используется активно
- `TELEMETRY_BROKER_URL` - не используется активно
- `OTEL_EXPORTER_ENDPOINT` - для мониторинга
- `AUTH_SERVICE_URL` - можно использовать заглушки

### Безопасность
- ⚠️ **НЕ коммитьте `.env` файл в Git**
- ✅ Используйте `.env.example` или `env.docker.example` как шаблон
- ✅ Для production используйте секреты Docker или внешние системы управления секретами





