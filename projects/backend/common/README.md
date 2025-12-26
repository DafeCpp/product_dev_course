# backend/common

Общие компоненты, переиспользуемые между микросервисами backend.

## Структура

- `backend_common/logging_config.py` - Конфигурация структурированного логирования (structlog)
- `backend_common/middleware/trace.py` - Middleware для trace_id и request_id
- `backend_common/db/pool.py` - Утилиты для работы с asyncpg connection pool
- `backend_common/repositories/base.py` - Базовый класс для репозиториев
- `backend_common/settings/base.py` - Базовый класс настроек сервисов
- `backend_common/core/exceptions.py` - Базовые классы исключений

## Использование

Пакет добавлен как зависимость в `pyproject.toml` сервисов:

```toml
backend-common = { path = "../../common", develop = true }
```

После установки зависимостей через `poetry install` можно импортировать:

```python
from backend_common.logging_config import configure_logging
from backend_common.middleware.trace import create_trace_middleware
from backend_common.repositories.base import BaseRepository
from backend_common.settings.base import BaseServiceSettings
```

## История

Общий код был извлечен из `experiment-service` и `auth-service` для устранения дублирования.
