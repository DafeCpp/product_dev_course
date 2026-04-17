# RFC-0012: config-service

**Статус:** Draft

**Автор(ы):** Ivan Khokhlov

**Дата:** 17-04-2026

**Тип:** New Service

---

## Резюме

Иногда нужно управлять поведением системы на лету, поднять таймауты походв в другие сервисы, базы данных, включение экспериментальной функциональности, добавление новых типов экспериментов, включение деградации при проблемах в сервисах. Сейчас этим нельзя управлять налету.

---

## Мотивация

Почему нужен новый сервис? Какие проблемы в текущей архитектуре он решает?

- **Проблема 1: Отсутствие runtime конфигурации** - Сейчас для изменения параметров поведения системы (таймауты, feature flags, пороги деградации) требуется изменить конфигурационные файлы и перезапустить сервис, что приводит к простою и невозможности оперативного реагирования на проблемы в production.

- **Проблема 2: Декентрализованное управление конфигурацией** - Конфигурация разбросана по разным сервисам в переменных окружения, ConfigMap'ах Kubernetes или локальных файлах, что усложняет аудит, версионирование и обеспечение консистентности.

- **Ограничения текущего подхода** - Нельзя оперативно реагировать на инциденты (например, увеличить таймаут к проблемному внешнему API или отключить проблемную функциональность без кодовых изменений и деплоя).

---

## Дизайн сервиса

### Основные компоненты

| Компонент | Описание | Технология |
|-----------|---------|-----------|
| API | REST эндпоинты (JSON over HTTP) | Python aiohttp |
| Хранилище конфигурации | Таблицы для разных типов конфигурации | PostgreSQL |
| Обработчик событий | Реакция на изменения конфигурации и распространение | Polling |
| Worker фоновой обработки | Асинхронные задачи (валидация, агрегация) | Р worker на основе asyncio |

### API

Основные эндпоинты для управления конфигурацией:

```
POST   /api/v1/config                 # Создание новой конфигурации
GET    /api/v1/config                 # Получение списка всех конфигураций (без фильтрации по сервису/проекту)
GET    /api/v1/config/{config_id}     # Получение конкретной конфигурации по ID
PATCH  /api/v1/config/{config_id}     # Частичное обновление конфигурации
DELETE /api/v1/config/{config_id}     # Удаление конфигурации (soft delete)
POST   /api/v1/config/{config_id}/activate  # Активация конфигурации
POST   /api/v1/config/{config_id}/deactivate # Деактивация конфигурации
GET    /api/v1/config/{config_id}/history   # История изменений конфигурации
```

#### Внутренний эндпоинт для сервисов-потребителей

Сервисы платформы используют внутренний эндпоинт для получения набора конфигураций, актуальных для конкретного сервиса и проекта, с поддержкой кэширования через ETag/Last-Modified:

```
GET    /api/v1/configs/bulk?service={service_name}&project={project_id}  # Получение всех конфигураций для сервиса и проекта
```

**Параметры запроса:**
- `service` (обязательно): идентификатор сервиса (например, `experiment-service`, `telemetry-ingest`)
- `project` (опционально): идентификатор проекта, если конфигурации scoped по проектам

**Ответ:**
- JSON-объект вида `{ "configs": { "<key>": <value>, ... } }` где ключ — это имя конфигурации (например, `timeout_ms`, `feature_flag_enabled`), а значение — соответствующее значение из поля `value` конфигурации.
- Заголовок `ETag` — хеш всего набора конфигураций.
- Заголовок `Last-Modified` — timestamp самого свежего обновления среди возвращаемых конфигураций.

**Условные запросы:**
Клиент может передавать заголовок `If-None-Match` со значением предыдущего ETag. Если набор конфигураций не изменился, сервис возвращает **304 Not Modified** и пустое тело.

**Пример запроса/ответа:**

```http
GET /api/v1/configs/bulk?service=experiment-service&project=project-a
Accept: application/json
If-None-Match: "W/\"abc123\""
```

*Если конфигурации изменились:*
```http
HTTP/1.1 200 OK
Content-Type: application/json
ETag: "W/\"def456\""
Last-Modified: Thu, 17 Apr 2026 10:30:00 GMT

{
  "configs": {
    "timeout_ms": 5000,
    "max_retries": 3,
    "feature_new_ui_enabled": true
  }
}
```

*Если конфигурации не изменились:*
```http
HTTP/1.1 304 Not Modified
ETag: "W/\"abc123\""
Last-Modified: Thu, 17 Apr 2026 10:15:00 GMT
```

**Типы конфигурации (поле `config_type`):**
- `feature_flag` - булевые переключатели функциональности
- `timeout` - настройки таймаутов для внешних вызовов
- `rate_limit` - ограничения частоты запросов
- `circuit_breaker` - настройки срабатывания и восстановления
- `experiment_param` - параметры A/B тестов и экспериментов
- `degradation_rule` - правила перехода в режим деградации

**Пример запроса/ответа для feature flag:**

```json
// POST /api/v1/config
{
  "key": "new_payment_gateway_enabled",
  "config_type": "feature_flag",
  "description": "Включить новый шлюз платежей для эксперимента",
  "value": {
    "enabled": true,
    "rollout_percentage": 15
  },
  "conditions": {
    "user_segment": "premium",
    "geo": ["US", "EU"]
  },
  "metadata": {
    "experiment_id": "exp_payment_v2",
    "start_date": "2026-04-18",
    "end_date": "2026-05-18"
  }
}

// Response 201
{
  "id": "a1b2c3d4-e5f6-7890-g1h2-i3j4k5l6m7n8",
  "key": "new_payment_gateway_enabled",
  "config_type": "feature_flag",
  "description": "Включить новый шлюз платежей для эксперимента",
  "value": {
    "enabled": true,
    "rollout_percentage": 15
  },
  "conditions": {
    "user_segment": "premium",
    "geo": ["US", "EU"]
  },
  "metadata": {
    "experiment_id": "exp_payment_v2",
    "start_date": "2026-04-18",
    "end_date": "2026-05-18"
  },
  "is_active": true,
  "version": 1,
  "created_at": "2026-04-17T10:00:00Z",
  "updated_at": "2026-04-17T10:00:00Z",
  "created_by": "ivan.khokhlov@company.com"
}
```

### Интеграция с другими сервисами

**Какие сервисы вызывают новый сервис:**
- **auth-service** - проверяет доступ пользователей к изменению конфигурации (RBAC)
- **Все сервисы-platform** (experiment-service, telemetry-ingest, и т.д.) - периодически опрашивают внутренний эндпоинт `/api/v1/configs/bulk` с параметрами `service` и `project`, кэшируют ответ in-process и используют условные запросы (If-None-Match) для получения 304 при отсутствии изменений
- **Системы мониторинга и алертинга** - могут менять конфигурацию degradation правил при обнаружении проблем (через обычные API эндпоинты)

**На какие внешние сервисы зависит:**
- **PostgreSQL** - перманентное хранилище всех конфигураций и их истории
- **auth-service** - проверка прав доступа при изменении конфигурации (через HTTP)

---

## Реализация

### Структура проекта

```
projects/backend/services/config-service/
├── src/config_service/
│   ├── api/           # REST routes (v1)
│   │   ├── v1/
│   │   │   ├── routes/
│   │   │   │   ├── config.py
│   │   │   │   └── health.py
│   │   │   └── dependencies.py
│   │   └── __init__.py
│   ├── services/      # Бизнес-логика
│   │   ├── config_service.py
│   │   ├── cache_service.py
│   │   ├── event_service.py
│   │   └── validation_service.py
│   ├── repositories/  # Слой доступа к данным
│   │   ├── config_repository.py
│   │   └── history_repository.py
│   ├── models/        # Pydantic модели и SQLAlchemy модели
│   │   ├── config.py
│   │   ├── history.py
│   │   └── enums.py
│   ├── workers/       # Фоновые обработчики
│   │   └── config_worker.py
│   ├── utils/         # Вспомогательные функции
│   │   └── conditions.py
│   └── main.py        # Entry point
├── tests/
│   ├── unit/
│   │   ├── test_config_service.py
│   │   ├── test_cache_service.py
│   │   └── test_validation.py
│   ├── integration/
│   │   └── test_config_api.py
│   └── conftest.py
├── migrations/        # SQL миграции (Alembic)
│   ├── versions/
│   └── env.py
├── openapi/           # OpenAPI 3.1 спецификация
│   └── config-service.yaml
├── pyproject.toml
├── Dockerfile
└── README.md
```

### Порт и переменные окружения

- **Порт:** 8004 (следовать номерации сервисов: auth-8001, experiment-8002, telemetry-ingest-8003)
- **Переменные:**
  - `DATABASE_URL`: PostgreSQL connection string (обязательно)
  - `AUTH_SERVICE_URL`: auth-service endpoint для проверки прав (обязательно)
  - `CONFIG_CACHE_TTL_SECONDS`: TTL для кеша конфигурации в Redis (по умолчанию: 5)
  - `CONFIG_POLL_INTERVAL_SECONDS`: интервал опроса изменений для worker'ов (по умолчанию: 1)
  - `LOG_LEVEL`: уровень логирования (INFO/DEBUG/WARNING/ERROR)

### Миграции БД

Начальная схема для хранения конфигураций, scoped по сервису и проекту:

```sql
-- Основная таблица конфигураций
CREATE TABLE configs (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  service_name VARCHAR(100) NOT NULL,   -- например, 'experiment-service', 'telemetry-ingest'
  project_id VARCHAR(100),              -- nullable, если конфигурация не привязана к конкретному проекту
  key VARCHAR(255) NOT NULL,            -- имя конфигурации внутри сервиса/проекта
  config_type VARCHAR(50) NOT NULL,     -- feature_flag, timeout, rate_limit, etc.
  description TEXT,
  value JSONB NOT NULL,                 -- Собственное значение конфигурации
  conditions JSONB,                     -- Условия применения (segments, geo, etc.)
  metadata JSONB,                       -- Дополнительные данные (эксперимент, даты и т.д.)
  is_active BOOLEAN DEFAULT FALSE,
  version INTEGER DEFAULT 1,
  created_at TIMESTAMPTZ DEFAULT CURRENT_TIMESTAMP,
  updated_at TIMESTAMPTZ DEFAULT CURRENT_TIMESTAMP,
  created_by VARCHAR(255),              -- email или ID пользователя
  updated_by VARCHAR(255),
  UNIQUE (service_name, project_id, key) -- позволяем одно и то же имя конфигурации для разных сервисов/проектов
);

-- История изменений конфигураций (для аудита и отката)
CREATE TABLE config_history (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  config_id UUID REFERENCES configs(id) ON DELETE CASCADE,
  service_name VARCHAR(100) NOT NULL,
  project_id VARCHAR(100),
  key VARCHAR(255) NOT NULL,
  config_type VARCHAR(50) NOT NULL,
  description TEXT,
  value JSONB NOT NULL,
  conditions JSONB,
  metadata JSONB,
  is_active BOOLEAN,
  version INTEGER,
  changed_at TIMESTAMPTZ DEFAULT CURRENT_TIMESTAMP,
  changed_by VARCHAR(255),
  change_reason TEXT                 -- Почему было сделано изменение
);

-- Индексы для быстрого поиска
CREATE INDEX idx_configs_service ON configs(service_name);
CREATE INDEX idx_configs_project ON configs(project_id);
CREATE INDEX idx_configs_key ON configs(key);
CREATE INDEX idx_configs_type ON configs(config_type);
CREATE INDEX idx_configs_active ON configs(is_active) WHERE is_active = TRUE;
CREATE INDEX idx_config_history_config_id ON config_history(config_id);
CREATE INDEX idx_config_history_changed_at ON config_history(changed_at);

-- Триггер для автоматического обновления updated_at
CREATE OR REPLACE FUNCTION update_updated_at_column()
RETURNS TRIGGER AS $$
BEGIN
  NEW.updated_at = CURRENT_TIMESTAMP;
  RETURN NEW;
END;
$$ language 'plpgsql';

CREATE TRIGGER update_configs_updated_at
BEFORE UPDATE ON configs
FOR EACH ROW EXECUTE PROCEDURE update_updated_at_column();

-- Триггер для сохранения истории при изменении конфигурации
CREATE OR REPLACE FUNCTION log_config_change()
RETURNS TRIGGER AS $$
BEGIN
  INSERT INTO config_history (
    config_id, service_name, project_id, key, config_type, description, value, conditions,
    metadata, is_active, version, changed_by, change_reason
  ) VALUES (
    OLD.id, OLD.service_name, OLD.project_id, OLD.key, OLD.config_type, OLD.description, OLD.value,
    OLD.conditions, OLD.metadata, OLD.is_active, OLD.version,
    COALESCE(NEW.updated_by, OLD.updated_by, 'system'),
    NEW.change_reason
  );
  RETURN NEW;
END;
$$ language 'plpgsql';

CREATE TRIGGER log_config_update
AFTER UPDATE ON configs
FOR EACH ROW EXECUTE FUNCTION log_config_change();
```

---

## Тестирование

### Unit tests

- Тестирование бизнес-логики сервисов (config_service, cache_service, validation_service)
- Тестирование репозиториев (запросы к PostgreSQL с использованием моков или in-memory SQLite)
- Мокирование внешних зависимостей (auth-service для проверки прав, Redis и RabbitMQ)
- Тестирование Pydantic моделей и валидации входных данных
- Тестирование условий применения конфигурации (segments, geo, время и т.д.)

### Integration tests

- E2E сценарии через API: создание конфигурации → чтение → обновление → деактивация → удаление
- Проверка интеграции с auth-service: эндпоинты защищены и требуют соответствующих ролей
- Тестирование обработки одновременных обновлений конфигурации (гонки условий)

### Load tests (опционально, если критично для высоконагруженных систем)

Нагрузка на сервис будет небольшая, несколько десятков RPS. По этому пока не вижу смысла в нагрузочном тестировании

---

## Развёртывание

### Docker Compose (dev)

```yaml
config-service:
  image: config-service:latest
  ports:
    - "8004:8004"
  environment:
    DATABASE_URL: postgresql://user:pass@postgres:5432/config_service_db
    AUTH_SERVICE_URL: http://auth-service:8001
    CONFIG_CACHE_TTL_SECONDS: 5
    CONFIG_POLL_INTERVAL_SECONDS: 1
    LOG_LEVEL: INFO
  depends_on:
    - postgres
```

### Миграции в CI/CD

```bash
# При деплое на stage/prod
make config-service-migrate
```

### Мониторинг

- OpenTelemetry экспорт (OTLP) для трассировки запросов и операций
- Логирование в Loki (через Alloy) с уровнем INFO в продакшене
- Метрики в Prometheus: количество запросов, задержки, ошибки, размер кеша
- Графики/алерты в Grafama:
  - Задержки API эндпоинтов (p95 < 100ms)
  - Количество ошибок валидации и авторизации

---

## Альтернативы

Почему выбран этот подход вместо других?

- **Альтернатива 1: Использование только переменных окружения и ConfigMaps** - Описание: Хранить всю конфигурацию в переменных окружения или Kubernetes ConfigMaps. Плюсы: простота реализации, нет внешних зависимостей. Минусы: требует перезапуска сервисов при изменении, сложно управлять условиями и версионированием, нет истории изменений.

- **Альтернатива 2: Сторонние решения (Consul, etcd, Apache Zookeeper)** - Описание: Использовать специализированные системы для distributed конфигурации. Плюсы: готовые решения, поддержка наблюдения за изменениями, сильная консистентность. Минусы: дополнительная сложность в инфраструктуре, требуется изучение новой технологии, потенциальный vendor lock-in.

- **Выбранный подход:** собственный сервис на основе PostgreSQL + Redis лучше, потому что:
  1. Использует уже существующие технологии в стеке (PostgreSQL, Redis, RabbitMQ)
  2. Позволяет гибко определять схему конфигурации под наши нужды (типы, условия, метаданные)
  3. Обеспечивает полный контроль над производительностью и оптимизацией
  4. Легко интегрируется с существующими системами мониторинга и алертинга
  5. Позволяет реализовать сложную логику применения конфигурации (условия, сегментация, время)
  6. Обеспечивает историю изменений и аудит из коробки

---

## Известные ограничения / Tech Debt

- **Ограничение 3: Ограничение размера значения** - Поле `value` типа JSONB имеет практический лимит размера (~1GB), но extremely большие конфигурации могут влиять на производительность. Следует документировать рекомендуемый максимальный размер.

---

## План реализации

### Phase 1: MVP (неделя 1)

- [ ] Структура проекта + Dockerfile
- [ ] API базовые эндпоинты (CRUD для конфигураций)
- [ ] БД миграции (таблицы configs и config_history)
- [ ] Unit tests (сервисы, репозитории, валидация)
- [ ] Интеграция с Redis для кеширования

### Phase 2: Интеграция (неделя 2)

- [ ] Интеграция с auth-service (проверка прав через middleware)
- [ ] Integration tests (полные сценарии через API)
- [ ] OpenAPI 3.1 спецификация
- [ ] Health check эндпоинт

### Phase 3: Production (неделя 3)

- [ ] OTLP/Loki конфигурация (трассировка и логирование)
- [ ] Мониторинг/алерты (задержки, ошибки, hit rate кеша)
- [ ] Documentation (руководство по использованию для сервисов-потребителей)
- [ ] Security audit (проверка авторизации и валидации входных данных)

---

## Ссылки

- Related ADR: [docs/adr/XXXX-название.md](../adr/)
- Issue/Epic: [GitHub issue #XXX](https://github.com/)
- Slack thread: (если есть обсуждение)

---

## История изменений

| Дата | Автор | Изменение |
|------|-------|----------|
| 2026-04-17 | [Ivan Khokhlov] | Первая версия |
