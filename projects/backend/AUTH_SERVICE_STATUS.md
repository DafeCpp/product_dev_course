# Статус Auth Service для MVP

## Обзор

Документ описывает реализацию Auth Service - сервиса аутентификации для Experiment Tracking Platform.

## ✅ Реализовано

### 1. Регистрация пользователей
- ✅ `POST /auth/register` - регистрация нового пользователя
  - Валидация данных (username, email, password)
  - Проверка уникальности username и email
  - Хеширование паролей с использованием bcrypt
  - Создание пользователя в базе данных
  - Генерация JWT токенов (access и refresh)

**Статус:** Полностью реализовано.

### 2. Аутентификация (Login)
- ✅ `POST /auth/login` - вход пользователя
  - Проверка credentials (username, password)
  - Верификация пароля через bcrypt
  - Генерация JWT токенов (access и refresh)

**Статус:** Полностью реализовано.

### 3. Обновление токенов
- ✅ `POST /auth/refresh` - обновление access токена
  - Валидация refresh токена
  - Проверка существования пользователя
  - Генерация новых токенов (access и refresh)

**Статус:** Полностью реализовано.

### 4. Получение информации о пользователе
- ✅ `GET /auth/me` - информация о текущем пользователе
  - Извлечение user_id из access токена
  - Получение данных пользователя из БД
  - Возврат информации о пользователе

**Статус:** Полностью реализовано.

### 5. Выход (Logout)
- ✅ `POST /auth/logout` - выход пользователя
  - Placeholder endpoint (в production должен инвалидировать токены)

**Статус:** Реализовано как placeholder.

### 6. Смена пароля
- ✅ `POST /auth/change-password` - смена пароля пользователя
  - Требует авторизации (Bearer token)
  - Проверяет старый пароль
  - Обновляет пароль и сбрасывает флаг `password_change_required`

**Статус:** Полностью реализовано.

### 7. Health Check
- ✅ `GET /health` - проверка работоспособности сервиса

**Статус:** Полностью реализовано.

### 8. Пользователь по умолчанию
- ✅ Создаётся при применении миграции `001_initial_schema.sql`
- **Username:** `admin`
- **Password:** `admin123`
- **Email:** `admin@example.com`
- **Password Change Required:** `true` (требуется смена пароля при первом входе)

**Статус:** Реализовано через миграцию.

## Технические детали

### Технологический стек
- **Framework:** aiohttp 3.10+
- **Database:** PostgreSQL 16 через asyncpg
- **Authentication:** JWT (PyJWT)
- **Password Hashing:** bcrypt
- **Validation:** Pydantic DTO

### База данных
- **База данных:** `auth_db` (отдельная от `experiment_db`)
- **Таблица:** `users`
  - `id` (UUID, primary key)
  - `username` (text, unique)
  - `email` (text, unique)
  - `hashed_password` (text)
  - `password_change_required` (boolean, default false) - флаг обязательной смены пароля
  - `created_at` (timestamptz)
  - `updated_at` (timestamptz)

### Миграции
- ✅ `001_initial_schema.sql` - создание таблицы users с полем `password_change_required` и пользователем по умолчанию
- Скрипт миграций: `bin/migrate.py`

### JWT Токены
- **Access Token:**
  - TTL: 900 секунд (15 минут) по умолчанию
  - Используется для авторизации API запросов
  - Передаётся в заголовке `Authorization: Bearer <token>`

- **Refresh Token:**
  - TTL: 1209600 секунд (14 дней) по умолчанию
  - Используется для обновления access токена
  - Передаётся в теле запроса или cookie

### Безопасность
- ✅ Хеширование паролей с bcrypt (12 раундов по умолчанию)
- ✅ JWT токены с секретным ключом
- ✅ Валидация входных данных через Pydantic
- ✅ Проверка уникальности username и email
- ✅ Обработка ошибок без раскрытия деталей

## Интеграция

### С Auth Proxy
Auth Proxy проксирует запросы к Auth Service:
- `/auth/login` → `http://auth-service:8001/auth/login`
- `/auth/refresh` → `http://auth-service:8001/auth/refresh`
- `/auth/logout` → `http://auth-service:8001/auth/logout`
- `/auth/me` → `http://auth-service:8001/auth/me`

### С Experiment Service
Experiment Service может использовать Auth Service для:
- Валидации токенов (через заголовок `Authorization`)
- Получения информации о пользователе
- RBAC (Role-Based Access Control) - в будущем

## API Endpoints

| Endpoint | Method | Описание | Статус |
|----------|--------|----------|--------|
| `/auth/register` | POST | Регистрация пользователя | ✅ |
| `/auth/login` | POST | Вход пользователя | ✅ |
| `/auth/refresh` | POST | Обновление токена | ✅ |
| `/auth/logout` | POST | Выход | ✅ |
| `/auth/me` | GET | Информация о пользователе | ✅ |
| `/auth/change-password` | POST | Смена пароля | ✅ |
| `/health` | GET | Health check | ✅ |

## Переменные окружения

Основные переменные (см. `env.example`):
- `DATABASE_URL` - подключение к PostgreSQL
- `JWT_SECRET` - секретный ключ для JWT
- `ACCESS_TOKEN_TTL_SEC` - время жизни access токена
- `REFRESH_TOKEN_TTL_SEC` - время жизни refresh токена
- `BCRYPT_ROUNDS` - количество раундов для хеширования паролей
- `CORS_ALLOWED_ORIGINS` - разрешенные origins для CORS

## Структура проекта

```
auth-service/
├── src/
│   └── auth_service/
│       ├── api/          # API роуты
│       ├── core/         # Исключения
│       ├── db/           # Подключение к БД
│       ├── domain/       # Модели и DTO
│       ├── repositories/ # Репозитории
│       ├── services/     # Бизнес-логика
│       └── main.py       # Точка входа
├── migrations/           # SQL миграции
├── bin/                  # Утилиты (миграции)
└── tests/                # Тесты
```

## Запуск

### Локально
```bash
cd projects/backend/services/auth-service
poetry install
cp env.example .env
poetry run python -m auth_service.main
```

### Docker
```bash
make dev  # Запускает все сервисы, включая auth-service
```

### Миграции
```bash
poetry run python bin/migrate.py --database-url "postgresql://postgres:postgres@localhost:5432/auth_db"
```

## Примеры использования

### Регистрация
```bash
curl -X POST http://localhost:8001/auth/register \
  -H 'Content-Type: application/json' \
  -d '{
    "username": "testuser",
    "email": "test@example.com",
    "password": "testpass123"
  }'
```

### Вход
```bash
curl -X POST http://localhost:8001/auth/login \
  -H 'Content-Type: application/json' \
  -d '{
    "username": "testuser",
    "password": "testpass123"
  }'
```

### Получение информации о пользователе
```bash
curl -X GET http://localhost:8001/auth/me \
  -H 'Authorization: Bearer <access_token>'
```

### Смена пароля
```bash
curl -X POST http://localhost:8001/auth/change-password \
  -H 'Authorization: Bearer <access_token>' \
  -H 'Content-Type: application/json' \
  -d '{
    "old_password": "admin123",
    "new_password": "newsecurepassword123"
  }'
```

## Следующие шаги (не критично для MVP)

1. ✅ **Обязательная смена пароля при первом входе** - реализовано через флаг `password_change_required`
2. **Инвалидация токенов при logout** - хранение blacklist токенов
3. **Email верификация** - подтверждение email при регистрации
4. **Восстановление пароля** - reset password flow
5. **RBAC** - роли и права доступа
6. **Rate limiting** - ограничение количества запросов
7. **Audit logging** - логирование действий пользователей
8. **UI для смены пароля** - фронтенд должен показывать предупреждение при `password_change_required: true`

## Ссылки

- **README:** `projects/backend/services/auth-service/README.md`
- **Docker Compose:** `docker-compose.yml`
- **Environment:** `env.docker.example`

