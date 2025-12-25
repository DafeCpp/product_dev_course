# Code Style Guide

Руководство по стилю кода для курса "Продуктовая разработка бэкенда".

## Содержание

- [Python](#python)
- [TypeScript](#typescript)
- [SQL](#sql)
- [Git](#git)
- [Документация](#документация)
- [Общие принципы](#общие-принципы)

## Python

### Форматирование

**Инструменты:**
- **Black** для автоформатирования
- **Ruff** для linting
- **mypy** для проверки типов

```bash
# Установка
pip install black ruff mypy

# Использование
black .
ruff check .
mypy .
```

### PEP 8 и основные правила

```python
# ✅ ХОРОШО: snake_case для функций и переменных
def calculate_total_price(items: list[dict]) -> float:
    total_price = sum(item["price"] for item in items)
    return total_price

# ❌ ПЛОХО: camelCase
def calculateTotalPrice(items):
    totalPrice = sum(item["price"] for item in items)
    return totalPrice
```

### Type hints

**Обязательно** используйте type hints для всех функций и методов:

```python
# ✅ ХОРОШО
from typing import Optional
from datetime import datetime

async def get_user_by_id(user_id: int) -> Optional[dict]:
    """Получить пользователя по ID."""
    # реализация
    pass

async def create_order(
    user_id: int,
    items: list[dict],
    created_at: datetime
) -> dict:
    """Создать новый заказ."""
    # реализация
    pass

# ❌ ПЛОХО: нет типов
async def get_user_by_id(user_id):
    pass
```

### Структура aiohttp приложения

```python
# main.py - точка входа
from aiohttp import web
from .routes import setup_routes
from .db import setup_db, close_db


def create_app() -> web.Application:
    """Создать и настроить приложение."""
    app = web.Application()

    # Настройка
    setup_routes(app)

    # Lifecycle events
    app.on_startup.append(setup_db)
    app.on_cleanup.append(close_db)

    return app


if __name__ == '__main__':
    app = create_app()
    web.run_app(app, host='0.0.0.0', port=8000)
```

### Структура handlers

```python
# handlers/users.py
from aiohttp import web
from typing import Optional

async def get_user(request: web.Request) -> web.Response:
    """
    Получить пользователя по ID.

    Args:
        request: HTTP запрос с user_id в path params

    Returns:
        JSON response с данными пользователя

    Raises:
        HTTPNotFound: если пользователь не найден
    """
    user_id = int(request.match_info['user_id'])

    # Получение DB из app
    db = request.app['db']

    user = await db.fetch_one(
        "SELECT * FROM users WHERE id = :user_id",
        {"user_id": user_id}
    )

    if not user:
        raise web.HTTPNotFound(text="User not found")

    return web.json_response({
        "id": user.id,
        "username": user.username,
        "email": user.email
    })


async def create_user(request: web.Request) -> web.Response:
    """Создать нового пользователя."""
    data = await request.json()

    # Валидация через Pydantic
    from .schemas import UserCreate
    user_data = UserCreate(**data)

    # Сохранение в БД
    db = request.app['db']
    # ... логика создания

    return web.json_response(
        {"id": new_user_id, "username": user_data.username},
        status=201
    )
```

### Pydantic схемы для валидации

```python
# schemas.py
from pydantic import BaseModel, EmailStr, Field
from typing import Optional
from datetime import datetime


class UserBase(BaseModel):
    """Базовая схема пользователя."""
    username: str = Field(..., min_length=3, max_length=50)
    email: EmailStr


class UserCreate(UserBase):
    """Схема для создания пользователя."""
    password: str = Field(..., min_length=8)


class UserResponse(UserBase):
    """Схема ответа с пользователем."""
    id: int
    created_at: datetime

    class Config:
        from_attributes = True
```

### Работа с БД (SQLAlchemy)

```python
# models.py
from sqlalchemy import Column, Integer, String, DateTime, func
from sqlalchemy.ext.declarative import declarative_base

Base = declarative_base()


class User(Base):
    """Модель пользователя."""
    __tablename__ = "users"

    id = Column(Integer, primary_key=True)
    username = Column(String(50), unique=True, nullable=False)
    email = Column(String(100), unique=True, nullable=False)
    password_hash = Column(String(255), nullable=False)
    created_at = Column(DateTime, server_default=func.now())
    updated_at = Column(DateTime, onupdate=func.now())

    def __repr__(self) -> str:
        return f"<User(id={self.id}, username='{self.username}')>"
```

### Логирование

```python
import structlog
from typing import Any

logger = structlog.get_logger()


async def process_order(order_id: int) -> dict[str, Any]:
    """Обработать заказ."""
    logger.info("processing_order_started", order_id=order_id)

    try:
        # Логика обработки
        result = await _process_order_logic(order_id)
        logger.info(
            "processing_order_completed",
            order_id=order_id,
            status=result["status"]
        )
        return result
    except Exception as e:
        logger.error(
            "processing_order_failed",
            order_id=order_id,
            error=str(e),
            exc_info=True
        )
        raise
```

### Константы и конфигурация

```python
# config.py
from pydantic_settings import BaseSettings
from typing import Optional


class Settings(BaseSettings):
    """Настройки приложения."""

    # Database
    database_url: str
    database_pool_size: int = 10

    # Redis
    redis_url: str = "redis://localhost:6379"

    # Auth
    secret_key: str
    access_token_expire_minutes: int = 30

    # App
    debug: bool = False
    log_level: str = "INFO"

    class Config:
        env_file = ".env"
        case_sensitive = False


# Singleton
settings = Settings()
```

### Обработка ошибок

```python
# middleware/errors.py
from aiohttp import web
import structlog

logger = structlog.get_logger()


@web.middleware
async def error_middleware(request: web.Request, handler):
    """Middleware для обработки ошибок."""
    try:
        return await handler(request)
    except web.HTTPException:
        # Пропускаем HTTP исключения
        raise
    except ValueError as e:
        logger.warning("validation_error", error=str(e))
        return web.json_response(
            {"error": "Validation error", "details": str(e)},
            status=400
        )
    except Exception as e:
        logger.error("unexpected_error", error=str(e), exc_info=True)
        return web.json_response(
            {"error": "Internal server error"},
            status=500
        )
```

## TypeScript

### Конфигурация

**tsconfig.json:**
```json
{
  "compilerOptions": {
    "target": "ES2020",
    "module": "commonjs",
    "lib": ["ES2020"],
    "outDir": "./dist",
    "rootDir": "./src",
    "strict": true,
    "esModuleInterop": true,
    "skipLibCheck": true,
    "forceConsistentCasingInFileNames": true,
    "resolveJsonModule": true,
    "noImplicitAny": true,
    "strictNullChecks": true
  },
  "include": ["src/**/*"],
  "exclude": ["node_modules", "dist", "**/*.test.ts"]
}
```

### Стиль кода

```typescript
// ✅ ХОРОШО: типизация, interface, async/await
interface User {
  id: number;
  username: string;
  email: string;
  createdAt: Date;
}

interface CreateUserDto {
  username: string;
  email: string;
  password: string;
}

class UserService {
  async getUserById(userId: number): Promise<User | null> {
    const user = await this.repository.findOne({ id: userId });
    return user;
  }

  async createUser(data: CreateUserDto): Promise<User> {
    const hashedPassword = await bcrypt.hash(data.password, 10);
    const user = await this.repository.create({
      ...data,
      passwordHash: hashedPassword,
    });
    return user;
  }
}

// ❌ ПЛОХО: нет типов, any
class UserService {
  async getUserById(userId: any) {
    const user = await this.repository.findOne({ id: userId });
    return user;
  }
}
```

### Express routes

```typescript
// routes/users.ts
import { Router, Request, Response, NextFunction } from 'express';
import { UserService } from '../services/user.service';
import { CreateUserDto } from '../dto/user.dto';

const router = Router();
const userService = new UserService();

router.get('/:id', async (req: Request, res: Response, next: NextFunction) => {
  try {
    const userId = parseInt(req.params.id, 10);
    const user = await userService.getUserById(userId);

    if (!user) {
      return res.status(404).json({ error: 'User not found' });
    }

    res.json(user);
  } catch (error) {
    next(error);
  }
});

router.post('/', async (req: Request, res: Response, next: NextFunction) => {
  try {
    const dto: CreateUserDto = req.body;
    const user = await userService.createUser(dto);
    res.status(201).json(user);
  } catch (error) {
    next(error);
  }
});

export default router;
```

## SQL

### Именование

```sql
-- ✅ ХОРОШО: snake_case, понятные имена
CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    username VARCHAR(50) UNIQUE NOT NULL,
    email VARCHAR(100) UNIQUE NOT NULL,
    password_hash VARCHAR(255) NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_users_email ON users(email);

-- ❌ ПЛОХО: camelCase, непонятные сокращения
CREATE TABLE usr (
    usrId SERIAL PRIMARY KEY,
    usrNm VARCHAR(50),
    pwd VARCHAR(255)
);
```

### Миграции (Alembic)

```python
"""add users table

Revision ID: 001_initial
Revises:
Create Date: 2025-10-29
"""
from alembic import op
import sqlalchemy as sa


def upgrade() -> None:
    """Применить миграцию."""
    op.create_table(
        'users',
        sa.Column('id', sa.Integer(), nullable=False),
        sa.Column('username', sa.String(50), nullable=False),
        sa.Column('email', sa.String(100), nullable=False),
        sa.Column('password_hash', sa.String(255), nullable=False),
        sa.Column('created_at', sa.DateTime(), server_default=sa.func.now()),
        sa.PrimaryKeyConstraint('id'),
        sa.UniqueConstraint('username'),
        sa.UniqueConstraint('email')
    )
    op.create_index('idx_users_email', 'users', ['email'])


def downgrade() -> None:
    """Откатить миграцию."""
    op.drop_index('idx_users_email')
    op.drop_table('users')
```

## Git

### Commit messages

Формат: `<type>(<scope>): <subject>`

**Types:**
- `feat`: новая функциональность
- `fix`: исправление бага
- `docs`: документация
- `style`: форматирование
- `refactor`: рефакторинг
- `test`: тесты
- `chore`: вспомогательные задачи

```bash
# ✅ ХОРОШО
feat(auth): add JWT token refresh endpoint
fix(orders): fix calculation of total price
docs(readme): update setup instructions
test(users): add tests for user creation

# ❌ ПЛОХО
updated files
fix bug
WIP
asdfasdf
```

### Branches

```bash
# Feature branches
feature/user-authentication
feature/order-processing

# Fix branches
fix/payment-validation
fix/database-connection

# Основные ветки
main           # production
develop        # development
```

## Документация

### Docstrings (Python)

```python
def calculate_discount(
    original_price: float,
    discount_percent: float,
    max_discount: Optional[float] = None
) -> float:
    """
    Вычислить цену со скидкой.

    Args:
        original_price: Исходная цена товара
        discount_percent: Процент скидки (0-100)
        max_discount: Максимальная сумма скидки (optional)

    Returns:
        Финальная цена со скидкой

    Raises:
        ValueError: Если discount_percent не в диапазоне 0-100

    Examples:
        >>> calculate_discount(100.0, 10.0)
        90.0
        >>> calculate_discount(100.0, 50.0, max_discount=30.0)
        70.0
    """
    if not 0 <= discount_percent <= 100:
        raise ValueError("Discount percent must be between 0 and 100")

    discount_amount = original_price * (discount_percent / 100)

    if max_discount and discount_amount > max_discount:
        discount_amount = max_discount

    return original_price - discount_amount
```

### JSDoc (TypeScript)

```typescript
/**
 * Calculate discounted price
 *
 * @param originalPrice - Original product price
 * @param discountPercent - Discount percentage (0-100)
 * @param maxDiscount - Maximum discount amount (optional)
 * @returns Final price with discount applied
 * @throws {Error} If discountPercent is not in range 0-100
 *
 * @example
 * ```typescript
 * calculateDiscount(100, 10) // returns 90
 * calculateDiscount(100, 50, 30) // returns 70
 * ```
 */
function calculateDiscount(
  originalPrice: number,
  discountPercent: number,
  maxDiscount?: number
): number {
  if (discountPercent < 0 || discountPercent > 100) {
    throw new Error('Discount percent must be between 0 and 100');
  }

  let discountAmount = originalPrice * (discountPercent / 100);

  if (maxDiscount && discountAmount > maxDiscount) {
    discountAmount = maxDiscount;
  }

  return originalPrice - discountAmount;
}
```

### README для модулей

Каждый модуль/сервис должен иметь README с:

```markdown
# Service Name

## Описание
Краткое описание сервиса и его назначения

## Технологии
- Python 3.14 / TypeScript 5.0
- aiohttp / Express
- PostgreSQL
- Redis

## Установка
```bash
pip install -r requirements.txt
```

## Запуск
```bash
python main.py
```

## API Endpoints
- `GET /api/v1/users` - получить список пользователей
- `POST /api/v1/users` - создать пользователя

## Переменные окружения
- `DATABASE_URL` - URL для подключения к БД
- `SECRET_KEY` - секретный ключ для JWT

## Тесты
```bash
pytest
```
```

## Общие принципы

### 1. DRY (Don't Repeat Yourself)

```python
# ✅ ХОРОШО
def validate_email(email: str) -> bool:
    """Валидация email."""
    import re
    pattern = r'^[\w\.-]+@[\w\.-]+\.\w+$'
    return bool(re.match(pattern, email))

def register_user(email: str, password: str):
    if not validate_email(email):
        raise ValueError("Invalid email")
    # ...

def update_user_email(user_id: int, new_email: str):
    if not validate_email(new_email):
        raise ValueError("Invalid email")
    # ...

# ❌ ПЛОХО: дублирование логики
def register_user(email: str, password: str):
    import re
    pattern = r'^[\w\.-]+@[\w\.-]+\.\w+$'
    if not re.match(pattern, email):
        raise ValueError("Invalid email")
    # ...

def update_user_email(user_id: int, new_email: str):
    import re
    pattern = r'^[\w\.-]+@[\w\.-]+\.\w+$'
    if not re.match(pattern, new_email):
        raise ValueError("Invalid email")
    # ...
```

### 2. SOLID принципы

```python
# Single Responsibility Principle
class UserRepository:
    """Отвечает только за работу с БД."""
    async def create(self, user_data: dict) -> User:
        pass

    async def get_by_id(self, user_id: int) -> Optional[User]:
        pass

class UserService:
    """Отвечает только за бизнес-логику."""
    def __init__(self, repository: UserRepository):
        self.repository = repository

    async def register_user(self, data: UserCreate) -> User:
        # Валидация, хеширование пароля и т.д.
        pass
```

### 3. Избегайте магических чисел

```python
# ✅ ХОРОШО
MAX_LOGIN_ATTEMPTS = 5
PASSWORD_MIN_LENGTH = 8
TOKEN_EXPIRE_MINUTES = 30

if login_attempts > MAX_LOGIN_ATTEMPTS:
    raise TooManyAttemptsError()

# ❌ ПЛОХО
if login_attempts > 5:
    raise TooManyAttemptsError()
```

### 4. Fail Fast

```python
# ✅ ХОРОШО: ранняя валидация
async def create_order(user_id: int, items: list[dict]):
    if not items:
        raise ValueError("Items list cannot be empty")

    if user_id <= 0:
        raise ValueError("Invalid user_id")

    # Основная логика
    # ...

# ❌ ПЛОХО: поздняя валидация
async def create_order(user_id: int, items: list[dict]):
    # Много кода...
    # ...
    # ...
    if not items:  # Слишком поздно!
        raise ValueError("Items list cannot be empty")
```

### 5. Используйте context managers

```python
# ✅ ХОРОШО
async with aiohttp.ClientSession() as session:
    async with session.get(url) as response:
        data = await response.json()

# БД транзакции
async with db.transaction():
    await db.execute("INSERT INTO users ...")
    await db.execute("INSERT INTO profiles ...")
```

## Pre-commit hooks

Создайте `.pre-commit-config.yaml`:

```yaml
repos:
  - repo: https://github.com/psf/black
    rev: 23.10.0
    hooks:
      - id: black
        language_version: python3.14

  - repo: https://github.com/charliermarsh/ruff-pre-commit
    rev: v0.1.3
    hooks:
      - id: ruff
        args: [--fix, --exit-non-zero-on-fix]

  - repo: https://github.com/pre-commit/mirrors-mypy
    rev: v1.6.1
    hooks:
      - id: mypy
        additional_dependencies: [types-all]
```

Установка:
```bash
pip install pre-commit
pre-commit install
```

---

**Помните:** Чистый код важнее умного кода. Пишите код, который легко читать и понимать.

