# Неделя 6: Integration тесты с testsuite

## Цели недели
- Научиться писать интеграционные тесты с реальными зависимостями
- Освоить testsuite от Яндекса для тестирования микросервисов
- Понять концепции test fixtures и test isolation
- Узнать про моки внешних сервисов и БД
- Научиться работать с test coverage

## Теория

### Что такое Integration Tests?

**Integration тесты** проверяют взаимодействие между различными компонентами системы: API, БД, кэш, внешние сервисы и т.д.

**Отличия от Unit тестов:**

| Аспект | Unit Tests | Integration Tests |
|--------|------------|-------------------|
| Scope | Одна функция/метод | Несколько компонентов |
| Скорость | Очень быстрые (мс) | Медленные (секунды) |
| Зависимости | Моки | Реальные сервисы |
| Изоляция | Полная | Частичная |
| Цель | Логика | Взаимодействие |

### Зачем нужен testsuite?

**testsuite** - это библиотека от Яндекса для интеграционного тестирования микросервисов на Python.

**Преимущества:**
- 🚀 Автоматический запуск PostgreSQL, Redis, MongoDB и других сервисов
- 🔧 Удобные фикстуры для работы с БД
- 🧹 Автоматическая очистка данных между тестами
- 📝 Моки для внешних HTTP запросов
- ⚡ Быстрое выполнение тестов
- 🎯 Изоляция тестов друг от друга

**GitHub:** https://github.com/yandex/yandex-taxi-testsuite

### Установка testsuite

```bash
pip install testsuite[postgresql,redis]
```

Это установит testsuite с поддержкой PostgreSQL и Redis.

## Настройка testsuite

### 1. Структура проекта

```
my_service/
├── src/
│   ├── __init__.py
│   ├── app.py          # aiohttp приложение
│   ├── handlers.py     # HTTP handlers
│   ├── db.py           # Database models
│   └── config.py
├── tests/
│   ├── conftest.py     # pytest configuration
│   ├── test_api.py     # API тесты
│   └── test_db.py      # БД тесты
├── requirements.txt
└── pytest.ini
```

### 2. conftest.py - основные фикстуры

```python
# tests/conftest.py
import pytest
from testsuite.databases.pgsql import discover

from src.app import create_app
from src.db import init_db


# Обнаружение SQL миграций
pytest_plugins = ['pytest_aiohttp.plugin']


@pytest.fixture(scope='session')
def pgsql_local():
    """Настройка PostgreSQL для тестов."""
    return discover.find_schemas(
        'my_service',  # Имя схемы
        ['src/db/migrations'],  # Путь к миграциям
    )


@pytest.fixture
async def app(pgsql):
    """Создание aiohttp приложения для тестов."""
    db_url = pgsql['my_service'].get_uri()
    app = create_app(db_url=db_url)
    await init_db(app)
    yield app
    await app['db'].close()


@pytest.fixture
async def client(aiohttp_client, app):
    """HTTP клиент для тестирования API."""
    return await aiohttp_client(app)


@pytest.fixture
async def pg_db(pgsql):
    """Прямой доступ к PostgreSQL."""
    return pgsql['my_service']
```

### 3. pytest.ini

```ini
[pytest]
testpaths = tests
python_files = test_*.py
python_classes = Test*
python_functions = test_*

asyncio_mode = auto

# testsuite настройки
pgsql_local_database_prefix = test_
```

## Примеры интеграционных тестов

### Пример 1: Тестирование CRUD API с реальной БД

```python
# tests/test_users_api.py
import pytest


@pytest.mark.asyncio
async def test_create_user(client, pg_db):
    """Тест создания пользователя через API."""
    # Arrange
    user_data = {
        "username": "testuser",
        "email": "test@example.com",
        "password": "securepass123"
    }

    # Act
    response = await client.post('/api/users', json=user_data)

    # Assert
    assert response.status == 201
    data = await response.json()
    assert data['username'] == user_data['username']
    assert data['email'] == user_data['email']
    assert 'password' not in data  # Пароль не должен возвращаться
    assert 'id' in data

    # Проверяем, что пользователь действительно создан в БД
    cursor = pg_db.cursor()
    cursor.execute(
        'SELECT username, email FROM users WHERE id = %s',
        (data['id'],)
    )
    db_user = cursor.fetchone()
    assert db_user is not None
    assert db_user[0] == user_data['username']


@pytest.mark.asyncio
async def test_get_user(client, pg_db):
    """Тест получения пользователя."""
    # Arrange: создаем пользователя напрямую в БД
    cursor = pg_db.cursor()
    cursor.execute(
        """
        INSERT INTO users (username, email, password_hash)
        VALUES (%s, %s, %s)
        RETURNING id
        """,
        ('john_doe', 'john@example.com', 'hashed_password')
    )
    user_id = cursor.fetchone()[0]
    pg_db.commit()

    # Act
    response = await client.get(f'/api/users/{user_id}')

    # Assert
    assert response.status == 200
    data = await response.json()
    assert data['id'] == user_id
    assert data['username'] == 'john_doe'
    assert data['email'] == 'john@example.com'


@pytest.mark.asyncio
async def test_update_user(client, pg_db):
    """Тест обновления пользователя."""
    # Arrange
    cursor = pg_db.cursor()
    cursor.execute(
        """
        INSERT INTO users (username, email, password_hash)
        VALUES (%s, %s, %s)
        RETURNING id
        """,
        ('jane_doe', 'jane@example.com', 'hashed')
    )
    user_id = cursor.fetchone()[0]
    pg_db.commit()

    # Act
    update_data = {"email": "jane.new@example.com"}
    response = await client.patch(
        f'/api/users/{user_id}',
        json=update_data
    )

    # Assert
    assert response.status == 200
    data = await response.json()
    assert data['email'] == update_data['email']

    # Проверяем изменения в БД
    cursor.execute('SELECT email FROM users WHERE id = %s', (user_id,))
    updated_email = cursor.fetchone()[0]
    assert updated_email == update_data['email']


@pytest.mark.asyncio
async def test_delete_user(client, pg_db):
    """Тест удаления пользователя."""
    # Arrange
    cursor = pg_db.cursor()
    cursor.execute(
        """
        INSERT INTO users (username, email, password_hash)
        VALUES (%s, %s, %s)
        RETURNING id
        """,
        ('to_delete', 'delete@example.com', 'hashed')
    )
    user_id = cursor.fetchone()[0]
    pg_db.commit()

    # Act
    response = await client.delete(f'/api/users/{user_id}')

    # Assert
    assert response.status == 204

    # Проверяем удаление из БД
    cursor.execute('SELECT * FROM users WHERE id = %s', (user_id,))
    assert cursor.fetchone() is None
```

### Пример 2: Тестирование с моками внешних сервисов

```python
# tests/test_external_api.py
import pytest


@pytest.fixture
def mock_external_api(mockserver):
    """Мок внешнего API."""
    @mockserver.json_handler('/external-api/users')
    def handler(request):
        return {
            'id': 123,
            'name': 'External User',
            'verified': True
        }

    return handler


@pytest.mark.asyncio
async def test_fetch_external_user(client, mock_external_api):
    """Тест получения данных из внешнего API."""
    # Act
    response = await client.get('/api/external-users/123')

    # Assert
    assert response.status == 200
    data = await response.json()
    assert data['name'] == 'External User'
    assert data['verified'] is True

    # Проверяем, что мок был вызван
    assert mock_external_api.times_called == 1
```

### Пример 3: Тестирование с Redis

```python
# tests/test_cache.py
import pytest
import json


@pytest.fixture
async def redis_client(redis_store):
    """Redis клиент для тестов."""
    return redis_store


@pytest.mark.asyncio
async def test_caching_user_data(client, redis_client, pg_db):
    """Тест кэширования данных пользователя."""
    # Arrange: создаем пользователя
    cursor = pg_db.cursor()
    cursor.execute(
        """
        INSERT INTO users (username, email, password_hash)
        VALUES (%s, %s, %s)
        RETURNING id
        """,
        ('cached_user', 'cache@example.com', 'hashed')
    )
    user_id = cursor.fetchone()[0]
    pg_db.commit()

    # Act 1: Первый запрос (должен попасть в БД и закэшироваться)
    response1 = await client.get(f'/api/users/{user_id}')
    assert response1.status == 200
    data1 = await response1.json()

    # Assert: данные появились в кэше
    cache_key = f'user:{user_id}'
    cached_data = await redis_client.get(cache_key)
    assert cached_data is not None
    cached_user = json.loads(cached_data)
    assert cached_user['username'] == 'cached_user'

    # Act 2: Второй запрос (должен взяться из кэша)
    response2 = await client.get(f'/api/users/{user_id}')
    assert response2.status == 200
    data2 = await response2.json()

    # Assert: данные идентичны
    assert data1 == data2


@pytest.mark.asyncio
async def test_cache_invalidation(client, redis_client, pg_db):
    """Тест инвалидации кэша при обновлении."""
    # Arrange
    cursor = pg_db.cursor()
    cursor.execute(
        """
        INSERT INTO users (username, email, password_hash)
        VALUES (%s, %s, %s)
        RETURNING id
        """,
        ('update_cache', 'update@example.com', 'hashed')
    )
    user_id = cursor.fetchone()[0]
    pg_db.commit()

    # Первый запрос - заполняем кэш
    await client.get(f'/api/users/{user_id}')

    # Act: Обновляем пользователя
    response = await client.patch(
        f'/api/users/{user_id}',
        json={"email": "new@example.com"}
    )
    assert response.status == 200

    # Assert: кэш должен быть инвалидирован
    cache_key = f'user:{user_id}'
    cached_data = await redis_client.get(cache_key)
    assert cached_data is None

    # Новый запрос должен вернуть обновленные данные
    response = await client.get(f'/api/users/{user_id}')
    data = await response.json()
    assert data['email'] == 'new@example.com'
```

### Пример 4: Транзакционные тесты

```python
# tests/test_transactions.py
import pytest


@pytest.mark.asyncio
async def test_atomic_transfer(client, pg_db):
    """Тест атомарного перевода средств между счетами."""
    # Arrange: создаем два счета
    cursor = pg_db.cursor()
    cursor.execute(
        """
        INSERT INTO accounts (user_id, balance)
        VALUES (1, 1000), (2, 500)
        RETURNING id
        """
    )
    account_ids = [row[0] for row in cursor.fetchall()]
    pg_db.commit()

    # Act: переводим 300 со счета 1 на счет 2
    transfer_data = {
        "from_account_id": account_ids[0],
        "to_account_id": account_ids[1],
        "amount": 300
    }
    response = await client.post('/api/transfers', json=transfer_data)

    # Assert
    assert response.status == 201

    # Проверяем балансы
    cursor.execute(
        'SELECT balance FROM accounts WHERE id IN (%s, %s)',
        (account_ids[0], account_ids[1])
    )
    balances = [row[0] for row in cursor.fetchall()]
    assert balances[0] == 700  # 1000 - 300
    assert balances[1] == 800  # 500 + 300


@pytest.mark.asyncio
async def test_transfer_insufficient_funds(client, pg_db):
    """Тест перевода с недостаточным балансом - транзакция должна откатиться."""
    # Arrange
    cursor = pg_db.cursor()
    cursor.execute(
        """
        INSERT INTO accounts (user_id, balance)
        VALUES (1, 100), (2, 500)
        RETURNING id
        """
    )
    account_ids = [row[0] for row in cursor.fetchall()]
    pg_db.commit()

    # Act: пытаемся перевести больше, чем есть на счете
    transfer_data = {
        "from_account_id": account_ids[0],
        "to_account_id": account_ids[1],
        "amount": 500
    }
    response = await client.post('/api/transfers', json=transfer_data)

    # Assert
    assert response.status == 400

    # Балансы не должны измениться
    cursor.execute(
        'SELECT balance FROM accounts WHERE id IN (%s, %s)',
        (account_ids[0], account_ids[1])
    )
    balances = [row[0] for row in cursor.fetchall()]
    assert balances[0] == 100  # Не изменился
    assert balances[1] == 500  # Не изменился
```

## Test Coverage

### Измерение покрытия кода тестами

```bash
# Установка
pip install pytest-cov

# Запуск тестов с coverage
pytest --cov=src --cov-report=html --cov-report=term

# Результат
---------- coverage: platform darwin, python 3.14.0 -----------
Name                Stmts   Miss  Cover
---------------------------------------
src/__init__.py         0      0   100%
src/app.py             45      2    96%
src/handlers.py        78      5    94%
src/db.py              32      0   100%
---------------------------------------
TOTAL                 155      7    95%
```

### Настройка .coveragerc

```ini
[run]
source = src
omit =
    */tests/*
    */venv/*
    */__pycache__/*

[report]
precision = 2
show_missing = True
skip_covered = False

[html]
directory = htmlcov
```

### Интерпретация результатов

- **90-100%** - Отлично! ✅
- **70-89%** - Хорошо, но есть куда расти 📈
- **50-69%** - Недостаточно, нужно добавить тестов ⚠️
- **< 50%** - Критически мало тестов ❌

**Важно:** 100% coverage не гарантирует отсутствие багов! Но помогает найти нетестированный код.

## Best Practices

### 1. Изоляция тестов

```python
# ❌ Плохо: тесты зависят друг от друга
user_id = None

async def test_create_user(client):
    global user_id
    response = await client.post('/api/users', json={...})
    user_id = (await response.json())['id']

async def test_get_user(client):
    # Зависит от test_create_user
    response = await client.get(f'/api/users/{user_id}')


# ✅ Хорошо: каждый тест независим
async def test_create_user(client):
    response = await client.post('/api/users', json={...})
    assert response.status == 201

async def test_get_user(client, pg_db):
    # Создаем пользователя в фикстуре
    user_id = create_test_user(pg_db)
    response = await client.get(f'/api/users/{user_id}')
```

### 2. Понятные assert сообщения

```python
# ❌ Плохо
assert response.status == 200

# ✅ Хорошо
assert response.status == 200, \
    f"Expected 200, got {response.status}: {await response.text()}"
```

### 3. Используйте фикстуры для setup/teardown

```python
@pytest.fixture
async def test_user(pg_db):
    """Создание тестового пользователя."""
    cursor = pg_db.cursor()
    cursor.execute(
        "INSERT INTO users (username, email) VALUES (%s, %s) RETURNING id",
        ('testuser', 'test@example.com')
    )
    user_id = cursor.fetchone()[0]
    pg_db.commit()

    yield user_id

    # Cleanup автоматически делается testsuite
```

### 4. Тестируйте edge cases

```python
@pytest.mark.parametrize('invalid_email', [
    '',
    'not-an-email',
    '@example.com',
    'user@',
    'user @example.com',
])
async def test_create_user_invalid_email(client, invalid_email):
    """Тест создания пользователя с невалидным email."""
    response = await client.post('/api/users', json={
        "username": "testuser",
        "email": invalid_email,
        "password": "pass123"
    })
    assert response.status == 400
```

## Дополнительные материалы

### Видео
- [Integration Testing Best Practices](https://www.youtube.com/watch?v=QYCaaNz8emY)
- [Testing aiohttp Applications](https://www.youtube.com/watch?v=fJ69Yf7VN5E)

### Статьи
- [testsuite documentation](https://github.com/yandex/yandex-taxi-testsuite)
- [pytest-aiohttp](https://pytest-aiohttp.readthedocs.io/)
- [Test Isolation Patterns](https://martinfowler.com/articles/nonDeterminism.html)

### Книги
- "Python Testing with pytest" - Brian Okken

## Следующая неделя

На [Неделе 7](../week-07/README.md) изучим CI/CD с GitHub Actions, линтеры и structured logging! 🚀

---

**Удачи с integration тестами! 🔗**

