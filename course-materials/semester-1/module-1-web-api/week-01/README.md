# Неделя 1: Введение в бэкенд-разработку

## Технологический стек недели
- Python 3.11+
- aiohttp 3.x — базовый HTTP-фреймворк курса (сервер и клиент)
- asyncpg — подключение к PostgreSQL без ORM
- pytest, pytest-aiohttp — тестирование
- Docker/Docker Compose для локальной инфраструктуры

## Цели недели
- Понимание архитектуры клиент-сервер
- Изучение HTTP протокола
- Настройка рабочего окружения
- Создание первого HTTP сервера

## Теоретическая часть

### 1. Что такое бэкенд?

**Backend** (бэкенд) - серверная часть приложения, которая:
- Обрабатывает бизнес-логику
- Управляет данными (БД)
- Обеспечивает безопасность
- Масштабируется под нагрузку

**Frontend** - клиентская часть (браузер, мобильное приложение)

### 2. Архитектура клиент-сервер

```
┌──────────┐       HTTP Request       ┌──────────┐
│          │ ────────────────────────>│          │
│  Client  │                          │  Server  │
│          │ <────────────────────────│          │
└──────────┘       HTTP Response      └──────────┘
```

**Клиент:**
- Инициирует запросы
- Отображает данные пользователю
- Валидирует input (базово)

**Сервер:**
- Принимает запросы
- Обрабатывает логику
- Возвращает ответы
- Хранит данные

### 3. HTTP Протокол

**HTTP (HyperText Transfer Protocol)** - протокол передачи гипертекста

#### HTTP Request структура

```http
GET /api/v1/users/123 HTTP/1.1
Host: api.example.com
User-Agent: Mozilla/5.0
Accept: application/json
Authorization: Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...

```

**Части запроса:**
1. **Request Line**: Метод + URL + Версия HTTP
2. **Headers**: Метаданные запроса
3. **Body**: Данные (для POST/PUT/PATCH)

#### HTTP Response структура

```http
HTTP/1.1 200 OK
Content-Type: application/json
Content-Length: 85
Cache-Control: no-cache

{
  "id": 123,
  "username": "john_doe",
  "email": "john@example.com"
}
```

**Части ответа:**
1. **Status Line**: Версия + Статус код + Текст статуса
2. **Headers**: Метаданные ответа
3. **Body**: Данные ответа

#### HTTP Методы

| Метод | Описание | Идемпотентный | Безопасный |
|-------|----------|---------------|------------|
| GET | Получить ресурс | Да | Да |
| POST | Создать ресурс | Нет | Нет |
| PUT | Обновить ресурс полностью | Да | Нет |
| PATCH | Обновить ресурс частично | Нет | Нет |
| DELETE | Удалить ресурс | Да | Нет |
| HEAD | Как GET, но без body | Да | Да |
| OPTIONS | Получить доступные методы | Да | Да |

#### HTTP Status Codes

**1xx - Informational**
- 100 Continue

**2xx - Success**
- 200 OK - запрос выполнен
- 201 Created - ресурс создан
- 204 No Content - успех без тела ответа

**3xx - Redirection**
- 301 Moved Permanently
- 302 Found (временное перенаправление)
- 304 Not Modified

**4xx - Client Error**
- 400 Bad Request - неверный запрос
- 401 Unauthorized - требуется аутентификация
- 403 Forbidden - доступ запрещен
- 404 Not Found - ресурс не найден
- 405 Method Not Allowed - метод не поддерживается
- 409 Conflict - конфликт
- 418 I'm a teapot - я чайник
- 422 Unprocessable Entity - ошибка валидации
- 429 Too Many Requests - превышено количество запросов
- 451 Unavailable For Legal Reasons - недоступен по юридическим причинам

**5xx - Server Error**
- 500 Internal Server Error - ошибка сервера
- 502 Bad Gateway - ошибка шлюза
- 503 Service Unavailable - сервис недоступен

### 4. REST API principles

**REST (Representational State Transfer)** - архитектурный стиль для API

**Принципы REST:**

1. **Client-Server** - разделение ответственности
2. **Stateless** - каждый запрос независим
3. **Cacheable** - ответы можно кэшировать
4. **Uniform Interface** - единообразный интерфейс
5. **Layered System** - многоуровневая архитектура

**RESTful URL naming:**

```
# ✅ ХОРОШО
GET    /api/v1/users          # Список пользователей
GET    /api/v1/users/123      # Конкретный пользователь
POST   /api/v1/users          # Создать пользователя
PUT    /api/v1/users/123      # Обновить пользователя
DELETE /api/v1/users/123      # Удалить пользователя

GET    /api/v1/users/123/orders  # Заказы пользователя

# ❌ ПЛОХО
GET    /api/v1/getUsers
POST   /api/v1/createUser
GET    /api/v1/user-list
DELETE /api/v1/deleteUserById?id=123
```

## Практическая часть

### Задание 1: Настройка окружения

1. Установите Python 3.11+
2. Установите VS Code или PyCharm
3. Создайте виртуальное окружение
4. Установите aiohttp

```bash
# Создание проекта
mkdir week-01-http-server
cd week-01-http-server

# Виртуальное окружение
python3 -m venv venv
source venv/bin/activate  # Linux/Mac
# venv\Scripts\activate  # Windows

# Установка aiohttp
pip install aiohttp
pip freeze > requirements.txt
```

### Задание 2: Простейший HTTP сервер

Создайте файл `server.py`:

```python
from aiohttp import web


async def hello(request: web.Request) -> web.Response:
    """Приветствие."""
    return web.Response(text="Hello, World!")


async def health(request: web.Request) -> web.Response:
    """Health check endpoint."""
    return web.json_response({"status": "healthy"})


def create_app() -> web.Application:
    """Создать приложение."""
    app = web.Application()
    app.router.add_get('/', hello)
    app.router.add_get('/health', health)
    return app


if __name__ == '__main__':
    app = create_app()
    web.run_app(app, host='0.0.0.0', port=8000)
```

Запустите:
```bash
python server.py
```

Протестируйте:
```bash
# В другом терминале
curl http://localhost:8000/
curl http://localhost:8000/health
```

### Задание 3: HTTP Echo Server

Создайте сервер, который возвращает информацию о запросе:

```python
from aiohttp import web
import json


async def echo_handler(request: web.Request) -> web.Response:
    """
    Вернуть информацию о запросе.
    """
    # Информация о запросе
    request_info = {
        "method": request.method,
        "path": request.path,
        "query": dict(request.query),
        "headers": dict(request.headers),
        "remote": request.remote,
    }

    # Если есть body
    if request.can_read_body:
        try:
            body = await request.text()
            request_info["body"] = body
        except:
            request_info["body"] = None

    return web.json_response(request_info, indent=2)


def create_app() -> web.Application:
    app = web.Application()
    app.router.add_route('*', '/echo', echo_handler)
    return app


if __name__ == '__main__':
    app = create_app()
    web.run_app(app, host='0.0.0.0', port=8000)
```

Тестирование:
```bash
# GET запрос
curl "http://localhost:8000/echo?name=John&age=30"

# POST запрос
curl -X POST http://localhost:8000/echo \
  -H "Content-Type: application/json" \
  -d '{"message": "Hello, Server!"}'

# Другие методы
curl -X PUT http://localhost:8000/echo
curl -X DELETE http://localhost:8000/echo
```

### Задание 4: Калькулятор API

Создайте простое API для математических операций:

```python
from aiohttp import web
from typing import Dict, Any


async def add(request: web.Request) -> web.Response:
    """Сложение двух чисел."""
    try:
        a = float(request.query.get('a', 0))
        b = float(request.query.get('b', 0))
        result = a + b

        return web.json_response({
            "operation": "add",
            "a": a,
            "b": b,
            "result": result
        })
    except ValueError:
        return web.json_response(
            {"error": "Invalid number format"},
            status=400
        )


async def calculate(request: web.Request) -> web.Response:
    """
    Универсальный калькулятор.
    POST /calculate
    Body: {"operation": "add", "a": 10, "b": 5}
    """
    try:
        data = await request.json()
    except:
        return web.json_response(
            {"error": "Invalid JSON"},
            status=400
        )

    operation = data.get('operation')
    a = data.get('a')
    b = data.get('b')

    # Валидация
    if not all([operation, a is not None, b is not None]):
        return web.json_response(
            {"error": "Missing required fields: operation, a, b"},
            status=400
        )

    try:
        a = float(a)
        b = float(b)
    except (ValueError, TypeError):
        return web.json_response(
            {"error": "a and b must be numbers"},
            status=400
        )

    # Вычисление
    operations = {
        'add': lambda x, y: x + y,
        'subtract': lambda x, y: x - y,
        'multiply': lambda x, y: x * y,
        'divide': lambda x, y: x / y if y != 0 else None,
    }

    if operation not in operations:
        return web.json_response(
            {"error": f"Unknown operation: {operation}"},
            status=400
        )

    result = operations[operation](a, b)

    if result is None:
        return web.json_response(
            {"error": "Division by zero"},
            status=400
        )

    return web.json_response({
        "operation": operation,
        "a": a,
        "b": b,
        "result": result
    })


def create_app() -> web.Application:
    app = web.Application()

    # Routes
    app.router.add_get('/add', add)
    app.router.add_post('/calculate', calculate)

    return app


if __name__ == '__main__':
    app = create_app()
    web.run_app(app, host='0.0.0.0', port=8000)
```

Тестирование:
```bash
# GET метод
curl "http://localhost:8000/add?a=10&b=5"

# POST метод
curl -X POST http://localhost:8000/calculate \
  -H "Content-Type: application/json" \
  -d '{"operation": "add", "a": 10, "b": 5}'

curl -X POST http://localhost:8000/calculate \
  -H "Content-Type: application/json" \
  -d '{"operation": "multiply", "a": 7, "b": 8}'
```

## Дополнительные материалы

### Полезные ссылки
- [HTTP MDN](https://developer.mozilla.org/en-US/docs/Web/HTTP)
- [REST API Tutorial](https://restfulapi.net/)
- [aiohttp Documentation](https://docs.aiohttp.org/en/stable/)
- [HTTP Status Codes](https://httpstatuses.com/)

### Инструменты для тестирования API
- **curl** - командная строка (см. подробный гайд ниже)
- **HTTPie** - удобный CLI: `http GET localhost:8000/users`
- **Postman** - GUI для тестирования API
- **Insomnia** - альтернатива Postman

### Подробный гайд по curl

**curl** - это инструмент командной строки для отправки HTTP запросов. Это must-have инструмент для любого бэкенд-разработчика.

#### Установка curl

**macOS** (обычно предустановлен):
```bash
curl --version
```

**Linux:**
```bash
sudo apt install curl  # Ubuntu/Debian
sudo yum install curl  # CentOS/RHEL
```

**Windows:**
- Скачать с [curl.se](https://curl.se/download.html)
- Или использовать в Git Bash

#### Базовые примеры

**GET запрос:**
```bash
# Простой GET
curl http://localhost:8000/

# С параметрами
curl "http://localhost:8000/users?page=1&limit=10"

# С заголовками
curl -H "Authorization: Bearer token123" http://localhost:8000/api/users
```

**POST запрос:**
```bash
# POST с JSON данными
curl -X POST http://localhost:8000/api/users \
  -H "Content-Type: application/json" \
  -d '{"username": "john", "email": "john@example.com"}'

# POST из файла
curl -X POST http://localhost:8000/api/users \
  -H "Content-Type: application/json" \
  -d @user.json
```

**PUT запрос:**
```bash
# Полное обновление ресурса
curl -X PUT http://localhost:8000/api/users/123 \
  -H "Content-Type: application/json" \
  -d '{"username": "john_updated", "email": "john@example.com"}'
```

**PATCH запрос:**
```bash
# Частичное обновление
curl -X PATCH http://localhost:8000/api/users/123 \
  -H "Content-Type: application/json" \
  -d '{"email": "newemail@example.com"}'
```

**DELETE запрос:**
```bash
curl -X DELETE http://localhost:8000/api/users/123
```

#### Полезные опции curl

**-i / --include** - показать headers в ответе:
```bash
curl -i http://localhost:8000/api/users
# HTTP/1.1 200 OK
# Content-Type: application/json
# ...
```

**-v / --verbose** - подробный вывод (включая request headers):
```bash
curl -v http://localhost:8000/api/users
# > GET /api/users HTTP/1.1
# > Host: localhost:8000
# > User-Agent: curl/7.68.0
# ...
```

**-s / --silent** - тихий режим (без прогресс-бара):
```bash
curl -s http://localhost:8000/api/users
```

**-o / --output** - сохранить ответ в файл:
```bash
curl -o response.json http://localhost:8000/api/users
```

**-w / --write-out** - вывести дополнительную информацию:
```bash
# Время выполнения запроса
curl -w "\nTime: %{time_total}s\n" http://localhost:8000/api/users

# HTTP статус код
curl -w "\nStatus: %{http_code}\n" http://localhost:8000/api/users

# Размер ответа
curl -w "\nSize: %{size_download} bytes\n" http://localhost:8000/api/users
```

**-L / --location** - следовать редиректам:
```bash
curl -L http://example.com/redirect
```

**--max-time** - максимальное время выполнения:
```bash
curl --max-time 5 http://slow-api.example.com
```

#### Работа с аутентификацией

**Basic Authentication:**
```bash
curl -u username:password http://localhost:8000/api/users
# Или
curl -H "Authorization: Basic dXNlcm5hbWU6cGFzc3dvcmQ=" http://localhost:8000/api/users
```

**Bearer Token (JWT):**
```bash
curl -H "Authorization: Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9..." \
  http://localhost:8000/api/users
```

**API Key:**
```bash
curl -H "X-API-Key: your-api-key" http://localhost:8000/api/users
```

#### Работа с cookies

**Отправить cookie:**
```bash
curl -b "session_id=abc123" http://localhost:8000/api/users
```

**Сохранить cookies в файл:**
```bash
curl -c cookies.txt http://localhost:8000/api/login
```

**Использовать cookies из файла:**
```bash
curl -b cookies.txt http://localhost:8000/api/users
```

#### Загрузка файлов

**Загрузить файл:**
```bash
curl -X POST http://localhost:8000/api/upload \
  -F "file=@/path/to/file.jpg" \
  -F "description=My photo"
```

**Несколько файлов:**
```bash
curl -X POST http://localhost:8000/api/upload \
  -F "file1=@image1.jpg" \
  -F "file2=@image2.jpg"
```

#### Тестирование API с curl

**Пример скрипта тестирования:**
```bash
#!/bin/bash

BASE_URL="http://localhost:8000"

echo "1. Health check..."
curl -s $BASE_URL/health | jq

echo "\n2. Get all users..."
curl -s $BASE_URL/api/users | jq

echo "\n3. Create user..."
curl -s -X POST $BASE_URL/api/users \
  -H "Content-Type: application/json" \
  -d '{"username": "testuser", "email": "test@example.com"}' | jq

echo "\n4. Get user by ID..."
curl -s $BASE_URL/api/users/1 | jq

echo "\n5. Update user..."
curl -s -X PATCH $BASE_URL/api/users/1 \
  -H "Content-Type: application/json" \
  -d '{"email": "updated@example.com"}' | jq

echo "\n6. Delete user..."
curl -s -X DELETE $BASE_URL/api/users/1

echo "\nTests completed!"
```

**Сохраните как `test_api.sh` и запустите:**
```bash
chmod +x test_api.sh
./test_api.sh
```

#### Форматирование JSON с jq

**jq** - это утилита для работы с JSON в командной строке.

**Установка:**
```bash
# macOS
brew install jq

# Linux
sudo apt install jq
```

**Использование с curl:**
```bash
# Красивый вывод JSON
curl -s http://localhost:8000/api/users | jq

# Извлечь конкретное поле
curl -s http://localhost:8000/api/users | jq '.[0].username'

# Фильтрация
curl -s http://localhost:8000/api/users | jq '.[] | select(.age > 25)'

# Подсчет элементов
curl -s http://localhost:8000/api/users | jq 'length'
```

#### Практические примеры для курса

**Тестирование TODO API:**
```bash
# Создать TODO
curl -X POST http://localhost:8000/api/todos \
  -H "Content-Type: application/json" \
  -d '{"title": "Buy groceries", "completed": false}'

# Получить все TODO
curl http://localhost:8000/api/todos | jq

# Получить конкретный TODO
curl http://localhost:8000/api/todos/1 | jq

# Обновить TODO
curl -X PATCH http://localhost:8000/api/todos/1 \
  -H "Content-Type: application/json" \
  -d '{"completed": true}'

# Удалить TODO
curl -X DELETE http://localhost:8000/api/todos/1
```

**Тестирование аутентификации:**
```bash
# Регистрация
curl -X POST http://localhost:8000/api/auth/register \
  -H "Content-Type: application/json" \
  -d '{"username": "testuser", "email": "test@example.com", "password": "SecurePass123"}'

# Вход (сохраняем токен)
TOKEN=$(curl -s -X POST http://localhost:8000/api/auth/login \
  -H "Content-Type: application/json" \
  -d '{"username": "testuser", "password": "SecurePass123"}' \
  | jq -r '.access_token')

echo "Token: $TOKEN"

# Использование токена
curl -H "Authorization: Bearer $TOKEN" \
  http://localhost:8000/api/auth/me | jq
```

**Тестирование пагинации:**
```bash
# Первая страница
curl "http://localhost:8000/api/users?page=1&limit=10" | jq

# Вторая страница
curl "http://localhost:8000/api/users?page=2&limit=10" | jq
```

#### Советы и best practices

1. **Используйте -v для отладки:**
   ```bash
   curl -v http://localhost:8000/api/users
   ```

2. **Проверяйте статус код:**
   ```bash
   curl -w "\nHTTP Status: %{http_code}\n" http://localhost:8000/api/users
   ```

3. **Сохраняйте часто используемые команды в alias:**
   ```bash
   # В ~/.bashrc или ~/.zshrc
   alias api-get='curl -H "Content-Type: application/json"'
   alias api-post='curl -X POST -H "Content-Type: application/json"'
   ```

4. **Используйте переменные окружения:**
   ```bash
   export API_URL="http://localhost:8000"
   export API_TOKEN="your-token-here"

   curl -H "Authorization: Bearer $API_TOKEN" $API_URL/api/users
   ```

5. **Создавайте файлы с тестовыми данными:**
   ```bash
   # user.json
   {
     "username": "testuser",
     "email": "test@example.com",
     "password": "SecurePass123"
   }

   # Использование
   curl -X POST http://localhost:8000/api/users \
     -H "Content-Type: application/json" \
     -d @user.json
   ```

#### Альтернативы curl

Если curl кажется сложным, попробуйте:

**HTTPie** - более дружелюбный синтаксис:
```bash
# Установка
pip install httpie

# Использование
http GET http://localhost:8000/api/users
http POST http://localhost:8000/api/users username=john email=john@example.com
```

**Postman** - GUI инструмент с визуальным интерфейсом, коллекциями запросов и автоматическим тестированием.

### Книги
- "HTTP: The Definitive Guide" - David Gourley
- "RESTful Web APIs" - Leonard Richardson

## Вопросы для самопроверки

1. В чем разница между GET и POST запросами?
2. Когда использовать PUT, а когда PATCH?
3. Что означает "idempotent" метод?
4. Почему REST API называется stateless?
5. Какой статус код вернуть при создании ресурса?
6. Как передать параметры в GET запросе?
7. В чем разница между 401 и 403 статусами?
8. Что такое CORS и зачем он нужен?

## Следующая неделя

На [Неделе 2](../week-02/README.md) изучим CRUD операции подробнее: Pydantic для валидации, структуру aiohttp приложения и создание полноценного TODO API! 🚀

---

**Удачи с бэкенд-разработкой! 💻**

