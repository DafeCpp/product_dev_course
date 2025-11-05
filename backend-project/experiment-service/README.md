# Experiment Service

Сервис для сохранения, просмотра и наблюдения за экспериментами (аэродинамические, прочностные и другие типы).

## Возможности

- ✅ CRUD операции с экспериментами
- ✅ Управление запусками (runs) экспериментов
- ✅ Фильтрация по проекту, статусу, тегам
- ✅ Поиск по названию и описанию
- ✅ Поддержка различных типов экспериментов
- ✅ Метаданные в формате JSON
- ✅ Интеграция с RabbitMQ для событий

## ⚠️ Важно: Аутентификация

В текущей версии аутентификация находится в **режиме разработки**. Для использования сервиса:

1. **Development режим:** Установите переменную окружения `DEBUG=true` или `ENV=development`
   - В этом режиме токен может быть любым UUID (или будет использован дефолтный тестовый user_id)
   - Пример: `Authorization: Bearer 550e8400-e29b-41d4-a716-446655440000`

2. **Production режим:** Для production необходимо:
   - Реализовать интеграцию с Auth Service
   - Настроить валидацию JWT токенов
   - Обновить middleware в `src/middleware.py`

**Текущая реализация:** В development режиме токен используется как user_id (если это валидный UUID), иначе используется дефолтный тестовый ID.

## Запуск

### Быстрый запуск с Docker Compose (рекомендуется)

Самый простой способ запустить сервис с базой данных:

```bash
# Запуск всех сервисов (БД + experiment-service)
docker-compose up -d

# Просмотр логов
docker-compose logs -f experiment-service

# Остановка
docker-compose down

# Остановка с удалением данных БД
docker-compose down -v
```

Сервис будет доступен по адресу: http://localhost:8002

База данных будет автоматически инициализирована при первом запуске. Сервис автоматически создаст все необходимые таблицы.

**Быстрый старт:**
```bash
# Запуск
./start.sh

# Или запуск только БД для локальной разработки
./start.sh local
```

### Локальная разработка

#### Вариант 1: С Docker Compose для БД

```bash
# Запустить только БД
docker-compose up -d postgres

# Установка зависимостей
pip install -r requirements.txt

# Копировать пример конфигурации
cp env.example .env

# Настройка переменных окружения (или редактировать .env)
# Отредактируйте .env файл или установите переменные окружения:
export DATABASE_URL="postgresql://postgres:postgres@localhost:5432/experiment_db"
export PORT=8002
export DEBUG=true  # Для development режима

# Запуск сервиса
python main.py
```

#### Вариант 2: Локальная PostgreSQL

Убедитесь, что PostgreSQL установлен и запущен:

```bash
# Установка зависимостей
pip install -r requirements.txt

# Создание базы данных (если еще не создана)
createdb experiment_db

# Настройка переменных окружения
cp env.example .env
# Отредактируйте .env файл или установите переменные окружения

# Запуск
python main.py
```

### Docker (только контейнер сервиса)

Если БД уже запущена отдельно:

```bash
docker build -t experiment-service .
docker run -p 8002:8002 \
  -e DATABASE_URL="postgresql://postgres:postgres@host.docker.internal:5432/experiment_db" \
  experiment-service
```

## API Endpoints

### Experiments

- `GET /experiments` - список экспериментов
- `POST /experiments` - создание эксперимента
- `GET /experiments/{id}` - получение эксперимента
- `PUT /experiments/{id}` - обновление эксперимента
- `DELETE /experiments/{id}` - удаление эксперимента
- `GET /experiments/search?q=...` - поиск экспериментов

### Runs

- `GET /experiments/{id}/runs` - список runs эксперимента
- `POST /experiments/{id}/runs` - создание run
- `GET /runs/{id}` - получение run
- `PUT /runs/{id}` - обновление run
- `PUT /runs/{id}/complete` - завершение run
- `PUT /runs/{id}/fail` - пометить run как failed

## Примеры использования

### Создание эксперимента

```bash
curl -X POST http://localhost:8002/experiments \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer <token>" \
  -d '{
    "project_id": "550e8400-e29b-41d4-a716-446655440000",
    "name": "Аэродинамические испытания крыла",
    "description": "Тестирование профиля NACA 2412",
    "experiment_type": "aerodynamics",
    "tags": ["aerodynamics", "wing", "naca"],
    "metadata": {
      "wind_speed": "30 m/s",
      "angle_of_attack": "0-15 deg"
    }
  }'
```

### Создание run

```bash
curl -X POST http://localhost:8002/experiments/{experiment_id}/runs \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer <token>" \
  -d '{
    "name": "run_001",
    "parameters": {
      "wind_speed": 30,
      "angle_of_attack": 5,
      "reynolds_number": 1000000
    },
    "notes": "Первичное тестирование"
  }'
```

### Поиск экспериментов

```bash
curl "http://localhost:8002/experiments/search?q=аэродинамика&project_id={project_id}"
```

## Структура данных

### Experiment

- `id` - UUID
- `project_id` - UUID проекта
- `name` - название
- `description` - описание
- `experiment_type` - тип (aerodynamics, strength, etc.)
- `status` - статус (created, running, completed, failed, archived)
- `tags` - массив тегов
- `metadata` - дополнительные данные (JSON)
- `created_by` - UUID пользователя
- `created_at`, `updated_at` - временные метки

### Run

- `id` - UUID
- `experiment_id` - UUID эксперимента
- `name` - название запуска
- `parameters` - параметры запуска (JSON)
- `status` - статус (created, running, completed, failed)
- `started_at`, `completed_at` - временные метки
- `duration_seconds` - длительность в секундах
- `notes` - заметки
- `metadata` - дополнительные данные (JSON)

## События

Сервис публикует следующие события в RabbitMQ:

- `experiment.created`
- `experiment.updated`
- `experiment.deleted`
- `run.created`
- `run.started`
- `run.completed`
- `run.failed`

