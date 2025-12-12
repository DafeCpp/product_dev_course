# Локальная разработка с Docker

Документ описывает настройку и использование Docker для локальной разработки с поддержкой hot-reload.

## Обзор

Система поддерживает два режима работы:
- **Development режим** - с hot-reload, volumes для исходного кода, dev команды
- **Production режим** - оптимизированные образы, без hot-reload

## Быстрый старт

### 1. Подготовка окружения

```bash
# Скопируйте файл с переменными окружения
cp env.docker.example .env

# Скопируйте override файл для dev режима
cp docker-compose.override.yml.example docker-compose.override.yml

# При необходимости отредактируйте .env и docker-compose.override.yml
```

### 2. Запуск в Development режиме

```bash
# Запустить все сервисы с hot-reload
docker-compose up

# Или в фоновом режиме
docker-compose up -d

# Просмотр логов
docker-compose logs -f

# Логи конкретного сервиса
docker-compose logs -f experiment-service
```

### 3. Запуск в Production режиме

```bash
# Запустить без override файла (production образы)
docker-compose -f docker-compose.yml up

# Или временно переименовать override файл
mv docker-compose.override.yml docker-compose.override.yml.bak
docker-compose up
```

## Архитектура Hot-Reload

### Experiment Service (Python/aiohttp)

**Инструмент:** `watchfiles` (настроен в `docker-compose.override.yml`)

**Как работает:**
1. Исходный код монтируется через volume из `./backend/services/experiment-service/src`
2. `watchfiles` отслеживает изменения `.py` файлов в `./src`
3. При изменении автоматически перезапускается процесс `python -m experiment_service.main`

**Команда запуска:**
```bash
watchfiles 'python -m experiment_service.main' ./src
```

**Проверка работы:**
1. Откройте файл `backend/services/experiment-service/src/experiment_service/main.py`
2. Внесите изменение (например, добавьте комментарий)
3. Сохраните файл
4. В логах `docker-compose logs -f experiment-service` должно появиться сообщение о перезапуске

### Auth Proxy (Node.js/Fastify)

**Инструмент:** `ts-node-dev`

**Как работает:**
1. Исходный код монтируется через volume из `./frontend/apps/auth-proxy/src`
2. `ts-node-dev` компилирует TypeScript на лету и отслеживает изменения
3. При изменении автоматически перезапускает процесс

**Команда запуска:**
```bash
npm run dev  # выполняет: ts-node-dev --respawn --transpile-only src/index.ts
```

**Проверка работы:**
1. Откройте файл `frontend/apps/auth-proxy/src/index.ts`
2. Внесите изменение
3. Сохраните файл
4. В логах должно появиться сообщение о перезапуске

### Experiment Portal (React/Vite)

**Инструмент:** Vite HMR (Hot Module Replacement)

**Как работает:**
1. Исходный код монтируется через volumes
2. Vite dev server запускается на порту 3000
3. Vite отслеживает изменения и применяет их через HMR (без полной перезагрузки страницы)

**Команда запуска:**
```bash
npm run dev  # выполняет: vite
```

**Проверка работы:**
1. Откройте браузер на `http://localhost:3000`
2. Откройте файл компонента, например `frontend/apps/experiment-portal/src/pages/ExperimentsList.tsx`
3. Внесите изменение (например, измените текст)
4. Сохраните файл
5. Изменения должны появиться в браузере без перезагрузки страницы

## Структура Dockerfile'ов

### Multi-stage Build

Все Dockerfile'ы используют multi-stage build для оптимизации:

#### Experiment Service
- **Base stage:** Установка зависимостей через Poetry
- **Production:** Запуск приложения

#### Auth Proxy
- **Base stage:** Установка зависимостей
- **Development stage:** Запуск с `ts-node-dev` для hot-reload
- **Build stage:** Компиляция TypeScript
- **Production stage:** Минимальный runtime образ

#### Experiment Portal
- **Development stage:** Запуск Vite dev server
- **Build stage:** Сборка React приложения
- **Production stage:** Nginx для статических файлов

## Docker Compose Override

Файл `docker-compose.override.yml` автоматически загружается Docker Compose и переопределяет настройки из основного `docker-compose.yml`.

### Что переопределяется:

1. **Volumes** - монтирование исходного кода для hot-reload
2. **Commands** - использование dev команд вместо production
3. **Build targets** - использование development stages из Dockerfile'ов
4. **Environment variables** - установка `DEV_MODE=true`, `NODE_ENV=development`
5. **Ports** - дополнительные порты для отладки

### Пример использования:

```yaml
# docker-compose.override.yml
services:
  experiment-service:
    volumes:
      - ./backend/services/experiment-service/src:/app/src:ro
    command: watchfiles 'python -m experiment_service.main' --filter python ./src
```

## Troubleshooting

### Hot-reload не работает

**Проблема:** Изменения в коде не применяются автоматически

**Решения:**
1. Проверьте, что `docker-compose.override.yml` существует и правильно настроен
2. Убедитесь, что volumes правильно монтированы:
   ```bash
   docker-compose exec experiment-service ls -la /app/src
   ```
3. Проверьте логи на наличие ошибок:
   ```bash
   docker-compose logs experiment-service
   ```
4. Для Python: убедитесь, что `watchfiles` установлен:
   ```bash
   docker-compose exec experiment-service pip list | grep watchfiles
   ```

### Порты уже заняты

**Проблема:** `Error: bind: address already in use`

**Решения:**
1. Измените порты в `docker-compose.override.yml`:
   ```yaml
   ports:
     - "8003:8002"  # вместо 8002:8002
   ```
2. Или остановите процесс, использующий порт:
   ```bash
   # Linux/Mac
   lsof -ti:8002 | xargs kill -9

   # Windows
   netstat -ano | findstr :8002
   taskkill /PID <PID> /F
   ```

### Изменения не видны в контейнере

**Проблема:** Файлы изменены на хосте, но не видны в контейнере

**Решения:**
1. Проверьте правильность путей в volumes:
   ```yaml
   volumes:
     - ./backend/services/experiment-service/src:/app/src  # правильный путь
     # НЕ: - ./src:/app/src  (относительно compose файла)
   ```
2. Убедитесь, что файлы сохранены на диске
3. Перезапустите контейнер:
   ```bash
   docker-compose restart experiment-service
   ```

### Медленная работа hot-reload

**Проблема:** Hot-reload работает, но очень медленно

**Решения:**
1. Используйте `.dockerignore` для исключения ненужных файлов
2. Исключите большие директории из volumes:
   ```yaml
   volumes:
     - /app/node_modules  # исключить node_modules
     - /app/__pycache__   # исключить Python кэш
   ```
3. Для Windows/Mac используйте Docker Desktop с настроенным file sharing

### Ошибки при сборке образов

**Проблема:** `docker-compose build` завершается с ошибкой

**Решения:**
1. Очистите кэш Docker:
   ```bash
   docker-compose build --no-cache
   ```
2. Проверьте, что все зависимости установлены:
   ```bash
   # Для Python
   cd backend/services/experiment-service
   poetry install

   # Для Node.js
   cd frontend/apps/auth-proxy
   npm install
   ```
3. Проверьте версии в Dockerfile (Node.js, Python)

## Полезные команды

### Управление контейнерами

```bash
# Запуск всех сервисов
docker-compose up -d

# Остановка всех сервисов
docker-compose stop

# Остановка и удаление контейнеров
docker-compose down

# Пересборка образов
docker-compose build

# Пересборка без кэша
docker-compose build --no-cache

# Перезапуск конкретного сервиса
docker-compose restart experiment-service
```

### Работа с логами

```bash
# Все логи
docker-compose logs -f

# Логи конкретного сервиса
docker-compose logs -f experiment-service

# Последние 100 строк
docker-compose logs --tail=100 experiment-service
```

### Выполнение команд в контейнере

```bash
# Запуск shell в контейнере
docker-compose exec experiment-service sh

# Выполнение команды
docker-compose exec experiment-service python -m pytest

# Выполнение команды в новом контейнере
docker-compose run --rm experiment-service python -m pytest
```

### Работа с базой данных

```bash
# Подключение к PostgreSQL (порт 5433 на хосте)
docker-compose exec postgres psql -U postgres -d experiment_db

# Или с хоста (если установлен psql)
psql -h localhost -p 5433 -U postgres -d experiment_db

# Выполнение миграций
docker-compose exec experiment-service python bin/migrate.py
```

### Очистка

```bash
# Удаление контейнеров и volumes
docker-compose down -v

# Удаление всех образов проекта
docker-compose down --rmi all

# Полная очистка (контейнеры, образы, volumes, сети)
docker-compose down -v --rmi all
```

## Переменные окружения

Основные переменные окружения описаны в `env.docker.example`.

Для development режима важны:
- `DEV_MODE=true` - включение dev режима
- `NODE_ENV=development` - для Node.js сервисов
- `ENV=development` - для Python сервисов

## Следующие шаги

После настройки hot-reload можно:
1. Настроить отладку через IDE (VS Code, PyCharm)
2. Настроить интеграционные тесты в Docker
3. Добавить дополнительные сервисы (Redis, RabbitMQ) при необходимости
4. Настроить мониторинг и логирование

## Дополнительные ресурсы

- [Docker Compose Documentation](https://docs.docker.com/compose/)
- [Docker Volumes](https://docs.docker.com/storage/volumes/)
- [watchfiles Documentation](https://github.com/samuelcolvin/watchfiles)
- [Vite HMR](https://vitejs.dev/guide/features.html#hot-module-replacement)
- [ts-node-dev](https://github.com/wclr/ts-node-dev)

