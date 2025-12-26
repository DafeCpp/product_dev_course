# Auth Service

Сервис аутентификации для Experiment Tracking Platform.

## Возможности

- Регистрация пользователей
- Вход пользователей
- JWT токены (access и refresh)
- Обновление токенов
- Получение информации о текущем пользователе
- Хеширование паролей с использованием bcrypt

## API Endpoints

- `POST /auth/register` - Регистрация нового пользователя
- `POST /auth/login` - Вход пользователя
- `POST /auth/refresh` - Обновление access токена
- `POST /auth/logout` - Выход (placeholder)
- `GET /auth/me` - Информация о текущем пользователе
- `POST /auth/change-password` - Смена пароля
- `GET /health` - Health check

## Пользователь по умолчанию

После применения миграций создаётся пользователь по умолчанию:
- **Username:** `admin`
- **Password:** `admin123`
- **Email:** `admin@example.com`
- **Password Change Required:** `true` (требуется смена пароля при первом входе)

⚠️ **ВАЖНО:** В production обязательно смените пароль администратора при первом входе!

## Установка и запуск

### Локальная разработка

```bash
cd projects/backend/services/auth-service
poetry install
cp env.example .env
# Отредактируйте .env при необходимости
poetry run python -m auth_service.main
```

### Миграции БД

```bash
poetry run python bin/migrate.py --database-url "postgresql://postgres:postgres@localhost:5432/auth_db"
```

После применения миграций будет создан пользователь по умолчанию:
- **Username:** `admin`
- **Password:** `admin123`
- **Email:** `admin@example.com`
- ⚠️ **Требуется смена пароля при первом входе!**

### Docker

```bash
docker build -t auth-service:dev .
docker run -p 8001:8001 --env-file env.example auth-service:dev
```

## Переменные окружения

См. `env.example` для полного списка переменных окружения.

## Смена пароля

Если пользователь имеет флаг `password_change_required: true`, фронтенд должен:
1. Показать предупреждение о необходимости смены пароля
2. Перенаправить на страницу смены пароля
3. Заблокировать доступ к основному функционалу до смены пароля

Пример запроса на смену пароля:
```bash
curl -X POST http://localhost:8001/auth/change-password \
  -H 'Authorization: Bearer <access_token>' \
  -H 'Content-Type: application/json' \
  -d '{
    "old_password": "admin123",
    "new_password": "newsecurepassword123"
  }'
```

После успешной смены пароля флаг `password_change_required` будет установлен в `false`.

## Структура проекта

```
auth-service/
├── src/
│   └── auth_service/
│       ├── api/          # API роуты
│       ├── core/          # Исключения и утилиты
│       ├── db/            # Подключение к БД
│       ├── domain/        # Модели и DTO
│       ├── repositories/  # Репозитории для работы с БД
│       ├── services/      # Бизнес-логика
│       └── main.py        # Точка входа
├── migrations/            # SQL миграции
├── bin/                   # Утилиты (миграции)
└── tests/                 # Тесты

```

