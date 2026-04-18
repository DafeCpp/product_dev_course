# RFC-XXXX: [Название нового сервиса]

**Статус:** Draft | Review | Approved | Implemented | Rejected

**Автор(ы):** [Имя автора(ов)]

**Дата:** [YYYY-MM-DD]

**Тип:** New Service | Architecture | API | Infrastructure

---

## Резюме

Краткое описание предложения (2-3 предложения). Чем это решает текущие проблемы?

---

## Мотивация

Почему нужен новый сервис? Какие проблемы в текущей архитектуре он решает?

- Проблема 1: описание и статус quo
- Проблема 2: описание и статус quo
- Ограничения текущего подхода

---

## Дизайн сервиса

### Основные компоненты

| Компонент | Описание | Технология |
|-----------|---------|-----------|
| API | REST / gRPC эндпоинты | Python aiohttp / FastAPI / ... |
| БД | Схема и таблицы | PostgreSQL / Redis / ... |
| Очередь | Обработка событий | RabbitMQ / Kafka |
| Cache | Кэширование данных | Redis |

### API

Основные эндпоинты:

```
POST   /api/v1/[resource]          # Создание
GET    /api/v1/[resource]          # Получение списка
GET    /api/v1/[resource]/{id}     # Получение по ID
PATCH  /api/v1/[resource]/{id}     # Обновление
DELETE /api/v1/[resource]/{id}     # Удаление
```

**Пример запроса/ответа:**

```json
// POST /api/v1/[resource]
{
  "field1": "value1",
  "field2": "value2"
}

// Response 201
{
  "id": "uuid",
  "field1": "value1",
  "field2": "value2",
  "created_at": "2026-04-17T10:00:00Z"
}
```

### Интеграция с другими сервисами

```
auth-service ──────┐
                   ├─→ [new-service]
experiment-service ┘    ├─→ PostgreSQL
                        ├─→ Redis
telemetry-ingest ──────→ └─→ RabbitMQ
```

Описать:
- Какие сервисы вызывают новый сервис?
- Какие события публикует новый сервис?
- На какие внешние сервисы зависит?

---

## Реализация

### Структура проекта

```
projects/backend/services/new-service/
├── src/new_service/
│   ├── api/           # REST routes
│   ├── services/      # Бизнес-логика
│   ├── repositories/  # БД
│   ├── models/        # Pydantic models
│   └── main.py        # Entry point
├── tests/
├── migrations/        # SQL миграции
├── openapi/           # OpenAPI 3.1 спецификация
├── pyproject.toml
└── Dockerfile
```

### Порт и переменные окружения

- **Порт:** 8XXX
- **Переменные:**
  - `DATABASE_URL`: PostgreSQL connection string
  - `REDIS_URL`: Redis URL
  - `RABBITMQ_URL`: RabbitMQ URL
  - `AUTH_SERVICE_URL`: auth-service endpoint

### Миграции БД

Начальная схема:

```sql
CREATE TABLE [table_name] (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  field1 VARCHAR(255) NOT NULL,
  field2 INTEGER,
  created_at TIMESTAMPTZ DEFAULT CURRENT_TIMESTAMP,
  updated_at TIMESTAMPTZ DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_[table_name]_field1 ON [table_name](field1);
```

---

## Тестирование

### Unit tests

- Тестирование сервисов (бизнес-логика)
- Тестирование репозиториев (DB queries)
- Мокирование зависимостей (auth-service, Redis, RabbitMQ)

### Integration tests

- E2E сценарии (создание → обновление → удаление)
- Проверка интеграции с другими сервисами
- Проверка сообщений в очереди

### Load tests

- Сценарии нагрузки (если критично)
- Ожидаемая throughput
- Лимиты и throttling

---

## Развёртывание

### Docker Compose (dev)

```yaml
new-service:
  image: new-service:latest
  ports:
    - "8XXX:8XXX"
  environment:
    DATABASE_URL: postgresql://user:pass@postgres:5432/new_service_db
    REDIS_URL: redis://redis:6379/0
    RABBITMQ_URL: amqp://guest:guest@rabbitmq:5672
    AUTH_SERVICE_URL: http://auth-service:8001
  depends_on:
    - postgres
    - redis
    - rabbitmq
```

### Миграции в CI/CD

```bash
# При деплое на stage/prod
make new-service-migrate
```

### Мониторинг

- OpenTelemetry экспорт (OTLP)
- Логирование в Loki (через Alloy)
- Метрики в Prometheus (если используются)
- Графики/алерты в Grafana

---

## Альтернативы

Почему выбран этот подход вместо других?

- **Альтернатива 1:** описание, плюсы/минусы
- **Альтернатива 2:** описание, плюсы/минусы
- **Выбранный подход:** почему лучше?

---

## Известные ограничения / Tech Debt

- Ограничение 1: описание, когда исправим
- Ограничение 2: описание, когда исправим

---

## План реализации

### Phase 1: MVP (неделя X)

- [ ] Структура проекта + Dockerfile
- [ ] API базовые эндпоинты (CRUD)
- [ ] БД миграции
- [ ] Unit tests

### Phase 2: Интеграция (неделя X+1)

- [ ] Интеграция с auth-service
- [ ] Обработка событий из очереди
- [ ] Integration tests
- [ ] OpenAPI spec

### Phase 3: Production (неделя X+2)

- [ ] Load tests
- [ ] OTLP/Loki конфигурация
- [ ] Мониторинг/алерты
- [ ] Documentation

---

## Ссылки

- Related ADR: [docs/adr/XXXX-название.md](../adr/)
- Issue/Epic: [GitHub issue #XXX](https://github.com/)
- Slack thread: (если есть обсуждение)

---

## История изменений

| Дата | Автор | Изменение |
|------|-------|----------|
| 2026-04-17 | [автор] | Инициал |
