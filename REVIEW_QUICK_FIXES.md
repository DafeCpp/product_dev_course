# Быстрые исправления после ревью

## 🔴 Критичные проблемы (исправить немедленно)

### 1. Добавить `.gitignore` в корень репозитория

**Проблема:** Отсутствует корневой `.gitignore`, что может привести к коммиту секретов и временных файлов.

**Решение:** Создать `.gitignore` в корне с содержимым:

```gitignore
# Python
__pycache__/
*.py[cod]
*$py.class
*.so
.Python
venv/
.venv/
env/
.env
*.egg-info/
dist/
build/
*.egg

# Backend
backend-project/**/__pycache__/
backend-project/**/*.pyc
backend-project/**/.env

# Frontend
frontend/node_modules/
frontend/dist/
frontend/.env
frontend/.env.local

# IDE
.vscode/
.idea/
*.swp
*.swo
*.sublime-project
*.sublime-workspace

# OS
.DS_Store
Thumbs.db
desktop.ini

# Logs
*.log
logs/
*.log.*

# Database
*.db
*.sqlite
*.sqlite3

# Testing
.pytest_cache/
.coverage
htmlcov/
.tox/
.hypothesis/

# Jupyter
.ipynb_checkpoints/

# Environment
.env.local
.env.*.local
```

### 2. Исправить auth middleware

**Файл:** `backend-project/experiment-service/src/middleware.py`

**Проблема:** `request['user_id'] = None` приводит к ошибкам при создании экспериментов.

**Решение:** Добавить временную валидацию JWT или mock для development:

```python
async def auth_middleware(request: Request, handler):
    """Middleware для проверки аутентификации."""
    public_paths = ['/health']

    if request.path in public_paths:
        return await handler(request)

    auth_header = request.headers.get('Authorization')
    if not auth_header or not auth_header.startswith('Bearer '):
        raise web.HTTPUnauthorized(text="Missing or invalid Authorization header")

    token = auth_header.replace('Bearer ', '')

    # ВРЕМЕННОЕ РЕШЕНИЕ ДЛЯ DEVELOPMENT
    # В продакшене нужно валидировать через Auth Service
    try:
        # Простая валидация формата токена (UUID)
        # В реальности здесь должен быть вызов Auth Service
        if settings.DEBUG or settings.ENV == 'development':
            # Для development: извлекаем user_id из токена (если это UUID)
            from uuid import UUID
            try:
                user_id = UUID(token)
                request['user_id'] = user_id
            except ValueError:
                # Если токен не UUID, используем дефолтный тестовый ID
                request['user_id'] = UUID('00000000-0000-0000-0000-000000000001')
        else:
            # В продакшене - валидация через Auth Service
            async with httpx.AsyncClient() as client:
                response = await client.get(
                    f"{settings.AUTH_SERVICE_URL}/verify",
                    headers={"Authorization": f"Bearer {token}"}
                )
                if response.status_code != 200:
                    raise web.HTTPUnauthorized(text="Invalid token")
                user_data = response.json()
                request['user_id'] = UUID(user_data['user_id'])

    except Exception as e:
        logger.error(f"Auth validation failed: {e}")
        raise web.HTTPUnauthorized(text="Invalid token")

    return await handler(request)
```

**Или более простое решение для MVP:**

```python
# В config.py добавить:
DEBUG: bool = os.getenv("DEBUG", "false").lower() == "true"

# В middleware.py:
if settings.DEBUG:
    # Для development: используем тестовый user_id
    from uuid import UUID
    request['user_id'] = UUID('00000000-0000-0000-0000-000000000001')
else:
    # TODO: Реальная валидация через Auth Service
    raise web.HTTPUnauthorized(text="Auth Service not implemented")
```

### 3. Добавить обработку ошибок валидации UUID

**Файл:** `backend-project/experiment-service/src/handlers/experiments.py`

**Проблема:** Невалидный UUID приводит к неинформативной ошибке.

**Решение:** Обернуть в try-except:

```python
async def get_experiment(request: Request) -> web.Response:
    """Получение эксперимента по ID."""
    try:
        experiment_id = UUID(request.match_info['experiment_id'])
    except ValueError:
        raise web.HTTPBadRequest(text="Invalid experiment ID format")

    experiment = await experiment_queries.get_experiment_by_id(experiment_id)
    # ... остальной код
```

Применить ко всем handlers, где используется UUID из path параметров.

---

## 🟠 Важные улучшения (желательно исправить)

### 4. Добавить `.env.example` в корень

Создать файл с примером всех необходимых переменных окружения.

### 5. Исправить логику валидации page_size

**Файл:** `backend-project/experiment-service/src/handlers/experiments.py`

**Текущий код:**
```python
if page_size > 100:
    page_size = 100
if page_size < 1:
    page_size = 50  # Неочевидно!
```

**Исправление:**
```python
page_size = max(1, min(page_size or 50, 100))
page = max(1, page or 1)
```

Или вынести в helper функцию (см. REVIEW.md).

### 6. Добавить предупреждение в README о auth

В `backend-project/experiment-service/README.md` добавить секцию:

```markdown
## ⚠️ Важно: Аутентификация

В текущей версии аутентификация находится в режиме разработки. Для production необходимо:
1. Реализовать интеграцию с Auth Service
2. Настроить валидацию JWT токенов
3. Обновить middleware в `src/middleware.py`
```

---

## 📝 Чеклист для быстрого исправления

- [ ] Создать `.gitignore` в корне
- [ ] Исправить `auth_middleware` (добавить временную валидацию)
- [ ] Добавить обработку ошибок UUID во всех handlers
- [ ] Исправить логику `page_size` валидации
- [ ] Добавить `.env.example` в корень
- [ ] Обновить README с предупреждением о auth

---

После выполнения этих исправлений репозиторий будет готов к использованию! 🚀

