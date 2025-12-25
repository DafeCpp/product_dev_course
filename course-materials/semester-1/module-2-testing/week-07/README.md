# Неделя 7: CI/CD с GitHub Actions, линтеры и structured logging

## Цели недели
- Понять принципы CI/CD (Continuous Integration / Continuous Deployment)
- Настроить автоматический запуск тестов и линтеров на GitHub Actions
- Освоить code quality инструменты: Ruff, Black, MyPy
- Научиться настраивать pre-commit hooks
- Внедрить structured logging с structlog
- Создать полный CI/CD pipeline для проекта

## Теория

### Что такое CI/CD?

**CI (Continuous Integration)** - непрерывная интеграция кода:
- 🔄 Автоматическая сборка при каждом коммите
- ✅ Запуск тестов
- 🔍 Проверка code quality
- 📊 Генерация отчетов

**CD (Continuous Deployment)** - непрерывная доставка:
- 🚀 Автоматический деплой в окружения
- 📦 Сборка Docker образов
- 🔐 Безопасность и секреты
- 📈 Мониторинг деплоя

### CI/CD Pipeline

```
┌──────────────┐
│   Git Push   │
│  to GitHub   │
└──────┬───────┘
       │
       ▼
┌─────────────────────────────────┐
│      GitHub Actions             │
│                                 │
│  1. Checkout Code              │
│  2. Setup Python               │
│  3. Install Dependencies       │
│  4. Run Linters (Ruff, MyPy)   │
│  5. Format Check (Black)       │
│  6. Run Tests (pytest)         │
│  7. Generate Coverage          │
│  8. Build Docker Image         │
│  9. Deploy (optional)          │
└──────┬──────────────────────────┘
       │
       ▼
┌──────────────┐
│   ✅ Success  │
│   ❌ Failure  │
└──────────────┘
```

## GitHub Actions

### Что такое GitHub Actions?

**GitHub Actions** - это встроенная CI/CD платформа от GitHub.

**Преимущества:**
- ✅ Бесплатно для публичных репозиториев
- ✅ Интеграция с GitHub
- ✅ Большая библиотека actions
- ✅ Матричные сборки (разные версии Python, OS)
- ✅ Секреты и переменные окружения

### Базовая структура workflow

Workflows находятся в `.github/workflows/*.yml`

```yaml
# .github/workflows/ci.yml
name: CI

# Когда запускать
on:
  push:
    branches: [ main, develop ]
  pull_request:
    branches: [ main ]

# Задачи
jobs:
  test:
    runs-on: ubuntu-latest

    steps:
      - uses: actions/checkout@v4
      - name: Setup Python
        uses: actions/setup-python@v5
        with:
          python-version: '3.14'

      - name: Run tests
        run: pytest
```

### Полный CI Pipeline

```yaml
# .github/workflows/ci.yml
name: CI Pipeline

on:
  push:
    branches: [ main, develop ]
  pull_request:
    branches: [ main ]

env:
  PYTHON_VERSION: '3.14'

jobs:
  lint:
    name: Code Quality Checks
    runs-on: ubuntu-latest

    steps:
      - name: Checkout code
        uses: actions/checkout@v4

      - name: Set up Python
        uses: actions/setup-python@v5
        with:
          python-version: ${{ env.PYTHON_VERSION }}
          cache: 'pip'

      - name: Install dependencies
        run: |
          python -m pip install --upgrade pip
          pip install ruff black mypy
          pip install -r requirements.txt

      - name: Run Ruff (linter)
        run: ruff check src tests

      - name: Check Black formatting
        run: black --check src tests

      - name: Run MyPy (type checking)
        run: mypy src

  test:
    name: Run Tests
    runs-on: ubuntu-latest

    services:
      postgres:
        image: postgres:15
        env:
          POSTGRES_USER: test
          POSTGRES_PASSWORD: test
          POSTGRES_DB: testdb
        ports:
          - 5432:5432
        options: >-
          --health-cmd pg_isready
          --health-interval 10s
          --health-timeout 5s
          --health-retries 5

      redis:
        image: redis:7-alpine
        ports:
          - 6379:6379
        options: >-
          --health-cmd "redis-cli ping"
          --health-interval 10s
          --health-timeout 5s
          --health-retries 5

    steps:
      - name: Checkout code
        uses: actions/checkout@v4

      - name: Set up Python
        uses: actions/setup-python@v5
        with:
          python-version: ${{ env.PYTHON_VERSION }}
          cache: 'pip'

      - name: Install dependencies
        run: |
          python -m pip install --upgrade pip
          pip install -r requirements.txt
          pip install pytest pytest-cov pytest-asyncio

      - name: Run tests with coverage
        env:
          DATABASE_URL: postgresql://test:test@localhost:5432/testdb
          REDIS_URL: redis://localhost:6379
        run: |
          pytest --cov=src --cov-report=xml --cov-report=term

      - name: Upload coverage to Codecov
        uses: codecov/codecov-action@v3
        with:
          file: ./coverage.xml
          fail_ci_if_error: false

      - name: Check coverage threshold
        run: |
          coverage report --fail-under=70

  build:
    name: Build Docker Image
    runs-on: ubuntu-latest
    needs: [lint, test]

    steps:
      - name: Checkout code
        uses: actions/checkout@v4

      - name: Set up Docker Buildx
        uses: docker/setup-buildx-action@v3

      - name: Build Docker image
        run: |
          docker build -t myapp:${{ github.sha }} .

      - name: Test Docker image
        run: |
          docker run --rm myapp:${{ github.sha }} python -c "import sys; print(sys.version)"
```

### Matrix Strategy - тестирование на разных версиях

```yaml
# .github/workflows/matrix-test.yml
name: Matrix Tests

on: [push, pull_request]

jobs:
  test:
    runs-on: ${{ matrix.os }}
    strategy:
      matrix:
        os: [ubuntu-latest, macos-latest, windows-latest]
        python-version: ['3.12', '3.13', '3.14']

    steps:
      - uses: actions/checkout@v4

      - name: Set up Python ${{ matrix.python-version }}
        uses: actions/setup-python@v5
        with:
          python-version: ${{ matrix.python-version }}

      - name: Install dependencies
        run: |
          pip install -r requirements.txt

      - name: Run tests
        run: pytest
```

### Секреты в GitHub Actions

```yaml
# Использование секретов
jobs:
  deploy:
    runs-on: ubuntu-latest
    steps:
      - name: Deploy to server
        env:
          API_KEY: ${{ secrets.API_KEY }}
          DATABASE_URL: ${{ secrets.DATABASE_URL }}
        run: |
          echo "Deploying with API key..."
```

**Настройка секретов:**
1. GitHub → Settings → Secrets and variables → Actions
2. New repository secret
3. Добавить `API_KEY`, `DATABASE_URL`, etc.

## Code Quality Tools

### 1. Ruff - быстрый Python linter

**Ruff** - современный, очень быстрый linter для Python (написан на Rust).

**Установка:**
```bash
pip install ruff
```

**Конфигурация:**

```toml
# pyproject.toml
[tool.ruff]
# Исключить файлы/директории
exclude = [
    ".git",
    ".venv",
    "venv",
    "__pycache__",
    "migrations",
]

# Длина строки
line-length = 88

# Версия Python
target-version = "py311"

[tool.ruff.lint]
# Включить правила
select = [
    "E",   # pycodestyle errors
    "W",   # pycodestyle warnings
    "F",   # pyflakes
    "I",   # isort
    "N",   # pep8-naming
    "UP",  # pyupgrade
    "B",   # flake8-bugbear
    "C4",  # flake8-comprehensions
    "SIM", # flake8-simplify
    "TCH", # flake8-type-checking
]

# Игнорировать правила
ignore = [
    "E501",  # line too long (handled by black)
]

[tool.ruff.lint.per-file-ignores]
# Игнорировать импорты в __init__.py
"__init__.py" = ["F401"]
# Игнорировать в тестах
"tests/**/*.py" = ["S101"]  # assert usage
```

**Использование:**
```bash
# Проверка
ruff check src tests

# Автоматическое исправление
ruff check --fix src tests

# Проверка конкретного файла
ruff check src/main.py
```

### 2. Black - code formatter

**Black** - автоматический форматировщик кода Python.

**Установка:**
```bash
pip install black
```

**Конфигурация:**

```toml
# pyproject.toml
[tool.black]
line-length = 88
target-version = ['py311']
include = '\.pyi?$'
exclude = '''
/(
    \.git
  | \.venv
  | venv
  | __pycache__
  | migrations
)/
'''
```

**Использование:**
```bash
# Форматирование
black src tests

# Проверка без изменений
black --check src tests

# Показать diff
black --diff src tests

# Форматирование конкретного файла
black src/main.py
```

### 3. MyPy - статическая проверка типов

**MyPy** - инструмент для статической проверки типов в Python.

**Установка:**
```bash
pip install mypy
```

**Конфигурация:**

```toml
# pyproject.toml
[tool.mypy]
python_version = "3.14"
warn_return_any = true
warn_unused_configs = true
disallow_untyped_defs = true
disallow_incomplete_defs = true
check_untyped_defs = true
no_implicit_optional = true
warn_redundant_casts = true
warn_unused_ignores = true
warn_no_return = true
strict_equality = true

# Игнорировать библиотеки без типов
[[tool.mypy.overrides]]
module = [
    "aioredis.*",
    "celery.*",
]
ignore_missing_imports = true
```

**Использование:**
```bash
# Проверка типов
mypy src

# Игнорировать ошибки импорта
mypy --ignore-missing-imports src

# Строгий режим
mypy --strict src
```

**Пример кода с типами:**

```python
# src/services/user_service.py
from typing import Optional, List
from src.models.user import User


class UserService:
    """Сервис для работы с пользователями."""

    def __init__(self, db_session) -> None:
        self.db = db_session

    async def get_by_id(self, user_id: int) -> Optional[User]:
        """Получить пользователя по ID."""
        result = await self.db.execute(
            select(User).where(User.id == user_id)
        )
        return result.scalar_one_or_none()

    async def get_all(
        self,
        skip: int = 0,
        limit: int = 100
    ) -> List[User]:
        """Получить список пользователей."""
        result = await self.db.execute(
            select(User).offset(skip).limit(limit)
        )
        return list(result.scalars().all())

    async def create(
        self,
        username: str,
        email: str,
        password: str
    ) -> User:
        """Создать пользователя."""
        from src.auth.jwt import get_password_hash

        user = User(
            username=username,
            email=email,
            hashed_password=get_password_hash(password)
        )
        self.db.add(user)
        await self.db.commit()
        await self.db.refresh(user)
        return user
```

## Pre-commit Hooks

### Что такое pre-commit?

**pre-commit** - это framework для управления git hooks.

**Git hooks** - это скрипты, которые выполняются автоматически при определенных git событиях (commit, push и т.д.).

**Установка:**
```bash
pip install pre-commit
```

### Конфигурация

```yaml
# .pre-commit-config.yaml
repos:
  # Ruff linter
  - repo: https://github.com/astral-sh/ruff-pre-commit
    rev: v0.1.5
    hooks:
      - id: ruff
        args: [--fix, --exit-non-zero-on-fix]
      - id: ruff-format

  # Black formatter
  - repo: https://github.com/psf/black
    rev: 23.10.1
    hooks:
      - id: black

  # MyPy type checker
  - repo: https://github.com/pre-commit/mirrors-mypy
    rev: v1.6.1
    hooks:
      - id: mypy
        additional_dependencies: [types-all]
        args: [--ignore-missing-imports]

  # Общие проверки
  - repo: https://github.com/pre-commit/pre-commit-hooks
    rev: v4.5.0
    hooks:
      - id: trailing-whitespace
      - id: end-of-file-fixer
      - id: check-yaml
      - id: check-added-large-files
      - id: check-json
      - id: check-merge-conflict
      - id: detect-private-key

  # Проверка requirements.txt
  - repo: https://github.com/pre-commit/pre-commit-hooks
    rev: v4.5.0
    hooks:
      - id: requirements-txt-fixer
```

### Установка hooks

```bash
# Установить pre-commit hooks
pre-commit install

# Запустить вручную на всех файлах
pre-commit run --all-files

# Запустить конкретный hook
pre-commit run black --all-files

# Обновить hooks до последних версий
pre-commit autoupdate
```

### Пример работы

```bash
# Пытаемся сделать commit
git commit -m "Add new feature"

# pre-commit автоматически запускается:
# ✅ Ruff check - PASSED
# ✅ Black formatting - PASSED
# ✅ MyPy type check - PASSED
# ✅ Trailing whitespace - PASSED
# ✅ End of file fixer - PASSED
# ✅ Check yaml - PASSED

# Если есть ошибки:
# ❌ Ruff check - FAILED
#    src/main.py:10:1: F401 'sys' imported but unused

# Коммит не создается, нужно исправить ошибки
```

## Structured Logging

### Зачем нужен structured logging?

**Обычные логи:**
```
2024-01-15 10:30:45 User johndoe logged in from 192.168.1.1
2024-01-15 10:31:12 Error processing payment for order 12345
```
❌ Сложно парсить
❌ Трудно фильтровать
❌ Нет структуры

**Structured логи:**
```json
{
  "timestamp": "2024-01-15T10:30:45Z",
  "event": "user_login",
  "username": "johndoe",
  "ip": "192.168.1.1",
  "level": "info"
}
{
  "timestamp": "2024-01-15T10:31:12Z",
  "event": "payment_error",
  "order_id": 12345,
  "error": "insufficient_funds",
  "level": "error"
}
```
✅ Легко парсить
✅ Простая фильтрация
✅ Структурированные данные

### structlog - библиотека для structured logging

**Установка:**
```bash
pip install structlog
```

### Базовая настройка

```python
# src/logging_config.py
import structlog
import logging


def configure_logging():
    """Настройка structured logging."""
    structlog.configure(
        processors=[
            # Добавляем уровень лога
            structlog.stdlib.add_log_level,
            # Добавляем timestamp
            structlog.processors.TimeStamper(fmt="iso"),
            # Добавляем информацию о вызове (файл, строка)
            structlog.processors.CallsiteParameterAdder(
                [
                    structlog.processors.CallsiteParameter.FILENAME,
                    structlog.processors.CallsiteParameter.LINENO,
                ]
            ),
            # Stack trace для исключений
            structlog.processors.StackInfoRenderer(),
            structlog.processors.format_exc_info,
            # JSON formatter
            structlog.processors.JSONRenderer(),
        ],
        wrapper_class=structlog.stdlib.BoundLogger,
        context_class=dict,
        logger_factory=structlog.stdlib.LoggerFactory(),
        cache_logger_on_first_use=True,
    )

    # Настройка стандартного logging
    logging.basicConfig(
        format="%(message)s",
        level=logging.INFO,
    )


# Вызываем при старте приложения
configure_logging()

# Получаем logger
logger = structlog.get_logger()
```

### Использование в коде

```python
# src/handlers/auth.py
import structlog
from aiohttp import web


logger = structlog.get_logger()


async def login(request: web.Request) -> web.Response:
    """Вход пользователя."""
    data = await request.json()
    username = data.get("username")

    logger.info(
        "login_attempt",
        username=username,
        ip=request.remote,
    )

    try:
        user = await authenticate(username, data.get("password"))

        if not user:
            logger.warning(
                "login_failed",
                username=username,
                reason="invalid_credentials",
            )
            raise web.HTTPUnauthorized()

        token = create_token(user.id)

        logger.info(
            "login_success",
            username=username,
            user_id=user.id,
        )

        return web.json_response({"token": token})

    except Exception as e:
        logger.error(
            "login_error",
            username=username,
            error=str(e),
            exc_info=True,
        )
        raise


async def create_user(request: web.Request) -> web.Response:
    """Создание пользователя."""
    data = await request.json()

    logger.info(
        "user_creation_started",
        username=data.get("username"),
        email=data.get("email"),
    )

    try:
        user = await user_service.create(data)

        logger.info(
            "user_created",
            user_id=user.id,
            username=user.username,
        )

        return web.json_response({"id": user.id}, status=201)

    except ValueError as e:
        logger.warning(
            "user_creation_failed",
            username=data.get("username"),
            reason=str(e),
        )
        raise web.HTTPBadRequest(reason=str(e))
```

### Context binding - привязка контекста

```python
import structlog


# Глобальный context
logger = structlog.get_logger()


# Привязка request_id
async def middleware(app, handler):
    async def middleware_handler(request):
        request_id = str(uuid.uuid4())

        # Привязываем request_id к логгеру
        log = logger.bind(request_id=request_id)
        request['log'] = log

        log.info(
            "request_started",
            method=request.method,
            path=request.path,
        )

        try:
            response = await handler(request)

            log.info(
                "request_completed",
                status=response.status,
            )
            return response

        except Exception as e:
            log.error(
                "request_failed",
                error=str(e),
                exc_info=True,
            )
            raise

    return middleware_handler


# В handler используем logger из request
async def get_user(request):
    log = request['log']  # Logger с request_id
    user_id = request.match_info['id']

    log.info("fetching_user", user_id=user_id)

    user = await db.get_user(user_id)

    log.info("user_fetched", user_id=user_id, username=user.username)

    return web.json_response(user.to_dict())
```

### Пример логов

```json
{
  "event": "request_started",
  "method": "POST",
  "path": "/api/auth/login",
  "request_id": "550e8400-e29b-41d4-a716-446655440000",
  "timestamp": "2024-01-15T10:30:45.123456Z",
  "level": "info"
}
{
  "event": "login_attempt",
  "username": "johndoe",
  "ip": "192.168.1.1",
  "request_id": "550e8400-e29b-41d4-a716-446655440000",
  "timestamp": "2024-01-15T10:30:45.234567Z",
  "level": "info"
}
{
  "event": "login_success",
  "username": "johndoe",
  "user_id": 123,
  "request_id": "550e8400-e29b-41d4-a716-446655440000",
  "timestamp": "2024-01-15T10:30:45.456789Z",
  "level": "info"
}
{
  "event": "request_completed",
  "status": 200,
  "request_id": "550e8400-e29b-41d4-a716-446655440000",
  "timestamp": "2024-01-15T10:30:45.567890Z",
  "level": "info"
}
```

## Best Practices

### 1. Fail Fast

```yaml
# Останавливаем pipeline при первой ошибке
jobs:
  test:
    steps:
      - name: Run tests
        run: pytest -x  # Остановка при первой ошибке
```

### 2. Кэширование зависимостей

```yaml
- name: Set up Python
  uses: actions/setup-python@v5
  with:
    python-version: '3.14'
    cache: 'pip'  # Кэширование pip packages

- name: Cache pre-commit
  uses: actions/cache@v3
  with:
    path: ~/.cache/pre-commit
    key: pre-commit-${{ hashFiles('.pre-commit-config.yaml') }}
```

### 3. Параллельное выполнение

```yaml
jobs:
  lint:
    # Линтеры запускаются параллельно с тестами
    runs-on: ubuntu-latest

  test:
    # Тесты запускаются параллельно с линтерами
    runs-on: ubuntu-latest

  build:
    # Build только после успешных lint + test
    needs: [lint, test]
    runs-on: ubuntu-latest
```

### 4. Логирование best practices

```python
# ✅ Хорошо - структурированные логи
logger.info(
    "user_action",
    action="create_order",
    user_id=user.id,
    order_id=order.id,
    amount=order.total,
)

# ❌ Плохо - строковые логи
logger.info(f"User {user.id} created order {order.id} for ${order.total}")
```

## Дополнительные материалы

### Документация
- [GitHub Actions Docs](https://docs.github.com/en/actions)
- [Ruff Documentation](https://docs.astral.sh/ruff/)
- [Black Documentation](https://black.readthedocs.io/)
- [MyPy Documentation](https://mypy.readthedocs.io/)
- [structlog Documentation](https://www.structlog.org/)
- [pre-commit Documentation](https://pre-commit.com/)

### Статьи
- [CI/CD Best Practices](https://about.gitlab.com/topics/ci-cd/)
- [Python Code Quality Tools](https://realpython.com/python-code-quality/)
- [Structured Logging in Python](https://structlog.org/en/stable/why.html)

### Видео
- [GitHub Actions Tutorial](https://www.youtube.com/watch?v=R8_veQiYBjI)
- [Python Code Quality](https://www.youtube.com/watch?v=M-UcUs7IMIM)

## Следующая неделя

На [Неделе 8](../../module-3-caching/week-08/README.md) изучим кэширование с Redis! ⚡

---

**Удачи с CI/CD и логированием! 🚀**

