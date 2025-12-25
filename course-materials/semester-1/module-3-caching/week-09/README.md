# Неделя 9: Async/await глубже и асинхронные паттерны

## Цели недели
- Глубоко понять event loop и как работает async/await
- Освоить продвинутые asyncio паттерны
- Научиться эффективно работать с concurrent requests
- Понять отличия между concurrency и parallelism
- Избегать типичных ошибок в асинхронном коде
- Оптимизировать производительность асинхронных приложений

## Теория

### Как работает async/await

**Синхронный код (блокирующий):**
```python
def fetch_user():
    time.sleep(1)  # Блокирует весь процесс!
    return {"id": 1, "name": "John"}

def main():
    user1 = fetch_user()  # Ждем 1 секунду
    user2 = fetch_user()  # Ждем еще 1 секунду
    user3 = fetch_user()  # Ждем еще 1 секунду
    # Всего: 3 секунды
```

**Асинхронный код (неблокирующий):**
```python
async def fetch_user():
    await asyncio.sleep(1)  # Не блокирует!
    return {"id": 1, "name": "John"}

async def main():
    # Запускаем параллельно
    results = await asyncio.gather(
        fetch_user(),
        fetch_user(),
        fetch_user()
    )
    # Всего: 1 секунда (все три запроса одновременно!)
```

### Event Loop - сердце asyncio

**Event Loop** - это бесконечный цикл, который управляет выполнением асинхронных задач.

```
┌──────────────────────────────────────────────┐
│           Event Loop                         │
│                                              │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  │
│  │  Task 1  │  │  Task 2  │  │  Task 3  │  │
│  │ (ready)  │  │(waiting) │  │(waiting) │  │
│  └──────────┘  └──────────┘  └──────────┘  │
│                                              │
│  1. Run Task 1 until await                  │
│  2. Switch to Task 2 (if ready)             │
│  3. Switch to Task 3 (if ready)             │
│  4. Back to Task 1 (if ready)               │
│  ... и так далее                            │
└──────────────────────────────────────────────┘
```

**Пример работы:**

```python
import asyncio


async def task(name: str, delay: float):
    print(f"{name}: Начал работу")
    await asyncio.sleep(delay)  # ← Точка переключения!
    print(f"{name}: Завершил работу")
    return f"Result from {name}"


async def main():
    # Создаем задачи
    task1 = asyncio.create_task(task("Task-1", 2))
    task2 = asyncio.create_task(task("Task-2", 1))
    task3 = asyncio.create_task(task("Task-3", 1.5))

    # Ждем завершения всех
    results = await asyncio.gather(task1, task2, task3)
    print(f"Результаты: {results}")


# Вывод:
# Task-1: Начал работу
# Task-2: Начал работу  ← Переключение!
# Task-3: Начал работу  ← Переключение!
# Task-2: Завершил работу (через 1 сек)
# Task-3: Завершил работу (через 1.5 сек)
# Task-1: Завершил работу (через 2 сек)
```

### Concurrency vs Parallelism

**Concurrency (Конкурентность):**
- Несколько задач **выполняются попеременно**
- Одно ядро CPU
- **asyncio** - это concurrency

```
CPU: [Task1][Task2][Task1][Task3][Task2][Task1]...
```

**Parallelism (Параллелизм):**
- Несколько задач **выполняются одновременно**
- Несколько ядер CPU
- **multiprocessing** - это parallelism

```
Core 1: [Task1]────────────────────────
Core 2: [Task2]────────────────────────
Core 3: [Task3]────────────────────────
```

**Когда что использовать:**

| Тип задачи | Решение |
|------------|---------|
| I/O операции (сеть, диск) | **asyncio** (concurrency) |
| CPU-интенсивные (вычисления) | **multiprocessing** (parallelism) |
| Смешанные | **asyncio** + **ProcessPoolExecutor** |

## Продвинутые asyncio паттерны

### 1. asyncio.gather() - параллельное выполнение

```python
import asyncio


async def fetch_user(user_id: int):
    """Получить пользователя из БД."""
    await asyncio.sleep(0.5)  # Имитация запроса
    return {"id": user_id, "name": f"User{user_id}"}


async def fetch_posts(user_id: int):
    """Получить посты пользователя."""
    await asyncio.sleep(0.3)
    return [{"id": 1, "title": "Post 1"}, {"id": 2, "title": "Post 2"}]


async def fetch_comments(user_id: int):
    """Получить комментарии пользователя."""
    await asyncio.sleep(0.4)
    return [{"id": 1, "text": "Comment 1"}]


async def get_user_profile(user_id: int):
    """Получить полный профиль пользователя."""
    # ❌ Плохо - последовательное выполнение (1.2 сек)
    # user = await fetch_user(user_id)
    # posts = await fetch_posts(user_id)
    # comments = await fetch_comments(user_id)

    # ✅ Хорошо - параллельное выполнение (0.5 сек)
    user, posts, comments = await asyncio.gather(
        fetch_user(user_id),
        fetch_posts(user_id),
        fetch_comments(user_id)
    )

    return {
        "user": user,
        "posts": posts,
        "comments": comments
    }


# Результат через 0.5 сек (вместо 1.2 сек)
asyncio.run(get_user_profile(1))
```

**Обработка ошибок в gather:**

```python
async def safe_gather():
    """gather с обработкой ошибок."""
    try:
        results = await asyncio.gather(
            fetch_user(1),
            fetch_user(2),
            fetch_user(3),
            return_exceptions=True  # Не падать при ошибке!
        )

        # Проверяем результаты
        for i, result in enumerate(results):
            if isinstance(result, Exception):
                print(f"Task {i} failed: {result}")
            else:
                print(f"Task {i} result: {result}")

    except Exception as e:
        print(f"Gather failed: {e}")
```

### 2. asyncio.wait() - контроль завершения

```python
import asyncio


async def task_with_wait():
    """Использование wait для контроля завершения."""
    tasks = [
        asyncio.create_task(fetch_user(i))
        for i in range(1, 6)
    ]

    # Ждем завершения ВСЕХ задач
    done, pending = await asyncio.wait(tasks)
    print(f"Завершено: {len(done)}, Осталось: {len(pending)}")

    # Получаем результаты
    results = [task.result() for task in done]
    return results
```

**Стратегии wait:**

```python
# 1. FIRST_COMPLETED - первая завершенная
done, pending = await asyncio.wait(
    tasks,
    return_when=asyncio.FIRST_COMPLETED
)
print(f"Первая задача завершена!")
# Отменяем остальные
for task in pending:
    task.cancel()


# 2. FIRST_EXCEPTION - первая с ошибкой
done, pending = await asyncio.wait(
    tasks,
    return_when=asyncio.FIRST_EXCEPTION
)


# 3. ALL_COMPLETED - все завершены (по умолчанию)
done, pending = await asyncio.wait(tasks)
```

### 3. asyncio.wait_for() - timeout

```python
import asyncio


async def slow_operation():
    """Медленная операция."""
    await asyncio.sleep(10)
    return "Done"


async def with_timeout():
    """Операция с таймаутом."""
    try:
        result = await asyncio.wait_for(
            slow_operation(),
            timeout=3.0  # 3 секунды максимум
        )
        return result

    except asyncio.TimeoutError:
        print("⏰ Операция превысила таймаут!")
        return None
```

**Применение в handlers:**

```python
# src/handlers/external_api.py
from aiohttp import web, ClientSession
import asyncio


async def fetch_external_data(request: web.Request) -> web.Response:
    """Запрос к внешнему API с таймаутом."""
    try:
        async with ClientSession() as session:
            # Таймаут 5 секунд
            async with asyncio.timeout(5):
                async with session.get('https://api.example.com/data') as resp:
                    data = await resp.json()
                    return web.json_response(data)

    except asyncio.TimeoutError:
        raise web.HTTPGatewayTimeout(
            reason="External API timeout"
        )

    except Exception as e:
        raise web.HTTPBadGateway(
            reason=f"External API error: {e}"
        )
```

### 4. asyncio.as_completed() - результаты по мере готовности

```python
import asyncio


async def fetch_with_delay(name: str, delay: float):
    """Задача с задержкой."""
    await asyncio.sleep(delay)
    return f"{name} completed after {delay}s"


async def process_as_completed():
    """Обработка результатов по мере готовности."""
    tasks = [
        fetch_with_delay("Fast", 1),
        fetch_with_delay("Medium", 2),
        fetch_with_delay("Slow", 3),
    ]

    # Получаем результаты по мере готовности
    for coro in asyncio.as_completed(tasks):
        result = await coro
        print(f"✅ {result}")
        # Можем обработать результат сразу!


# Вывод:
# ✅ Fast completed after 1s    (через 1 сек)
# ✅ Medium completed after 2s  (через 2 сек)
# ✅ Slow completed after 3s    (через 3 сек)
```

### 5. Task Groups (Python 3.14+)

```python
import asyncio


async def with_task_group():
    """Использование TaskGroup для управления задачами."""
    async with asyncio.TaskGroup() as tg:
        # Создаем задачи
        task1 = tg.create_task(fetch_user(1))
        task2 = tg.create_task(fetch_user(2))
        task3 = tg.create_task(fetch_user(3))

    # После выхода из контекста все задачи завершены
    # Если хоть одна упала - все отменяются!
    results = [task1.result(), task2.result(), task3.result()]
    return results
```

## Асинхронные контекстные менеджеры

### Базовый пример

```python
class AsyncResource:
    """Асинхронный контекстный менеджер."""

    async def __aenter__(self):
        """Вход в контекст."""
        print("📂 Opening resource...")
        await asyncio.sleep(0.1)  # Асинхронная инициализация
        self.resource = "Resource data"
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb):
        """Выход из контекста."""
        print("📁 Closing resource...")
        await asyncio.sleep(0.1)  # Асинхронная очистка
        self.resource = None


async def use_resource():
    """Использование асинхронного контекстного менеджера."""
    async with AsyncResource() as res:
        print(f"Using: {res.resource}")
```

### Практический пример - Database Transaction

```python
# src/db/transaction.py
from typing import Optional
import asyncpg


# В asyncpg транзакции работают через connection.transaction()

# Использование
async def transfer_money(
    pool: asyncpg.Pool,
    from_id: int,
    to_id: int,
    amount: float
):
    """Перевод денег с транзакцией."""
    async with pool.acquire() as conn:
        async with conn.transaction():
            # Снимаем со счета отправителя
            await conn.execute("""
                UPDATE accounts
                SET balance = balance - $1
                WHERE id = $2
            """, amount, from_id)

            # Добавляем на счет получателя
            await conn.execute("""
                UPDATE accounts
                SET balance = balance + $1
                WHERE id = $2
            """, amount, to_id)

            # При выходе из контекста - автоматический commit
            # Если ошибка - автоматический rollback!
```

### @asynccontextmanager декоратор

```python
from contextlib import asynccontextmanager


@asynccontextmanager
async def managed_db_connection():
    """Управляемое подключение к БД."""
    # Setup
    conn = await create_connection()
    print("✅ Connection opened")

    try:
        yield conn
    finally:
        # Cleanup
        await conn.close()
        print("❌ Connection closed")


# Использование
async def query_data():
    async with managed_db_connection() as conn:
        result = await conn.execute("SELECT * FROM users")
        return result
```

## Concurrent requests в aiohttp

### Параллельные запросы к БД

```python
# src/services/dashboard_service.py
import asyncio
from typing import Dict, Any


class DashboardService:
    """Сервис для дашборда с параллельными запросами."""

    async def get_dashboard_data(self, user_id: int) -> Dict[str, Any]:
        """Получить все данные для дашборда параллельно."""

        # Запускаем все запросы параллельно
        user, stats, notifications, recent_activity = await asyncio.gather(
            self._get_user(user_id),
            self._get_stats(user_id),
            self._get_notifications(user_id),
            self._get_recent_activity(user_id),
            return_exceptions=True  # Не падать при ошибке одного запроса
        )

        # Обработка результатов
        return {
            "user": user if not isinstance(user, Exception) else None,
            "stats": stats if not isinstance(stats, Exception) else {},
            "notifications": notifications if not isinstance(notifications, Exception) else [],
            "recent_activity": recent_activity if not isinstance(recent_activity, Exception) else [],
        }

    async def _get_user(self, user_id: int):
        async with async_session() as session:
            result = await session.execute(
                select(User).where(User.id == user_id)
            )
            return result.scalar_one_or_none()

    async def _get_stats(self, user_id: int):
        async with async_session() as session:
            # Параллельные запросы внутри
            posts_count, comments_count, likes_count = await asyncio.gather(
                session.scalar(select(func.count()).select_from(Post).where(Post.user_id == user_id)),
                session.scalar(select(func.count()).select_from(Comment).where(Comment.user_id == user_id)),
                session.scalar(select(func.count()).select_from(Like).where(Like.user_id == user_id)),
            )

            return {
                "posts": posts_count,
                "comments": comments_count,
                "likes": likes_count,
            }

    async def _get_notifications(self, user_id: int):
        async with async_session() as session:
            result = await session.execute(
                select(Notification)
                .where(Notification.user_id == user_id)
                .where(Notification.is_read == False)
                .order_by(Notification.created_at.desc())
                .limit(10)
            )
            return result.scalars().all()

    async def _get_recent_activity(self, user_id: int):
        async with async_session() as session:
            result = await session.execute(
                select(Activity)
                .where(Activity.user_id == user_id)
                .order_by(Activity.created_at.desc())
                .limit(20)
            )
            return result.scalars().all()
```

### Batch processing с semaphore

```python
import asyncio
from typing import List


async def process_item(item_id: int, semaphore: asyncio.Semaphore):
    """Обработать один элемент с ограничением конкурентности."""
    async with semaphore:
        # Только N задач одновременно!
        await asyncio.sleep(0.5)  # Имитация работы
        return f"Processed {item_id}"


async def batch_process(item_ids: List[int], max_concurrent: int = 5):
    """Обработка batch с ограничением конкурентности."""
    # Создаем semaphore
    semaphore = asyncio.Semaphore(max_concurrent)

    # Запускаем все задачи
    tasks = [
        process_item(item_id, semaphore)
        for item_id in item_ids
    ]

    # Ждем завершения всех
    results = await asyncio.gather(*tasks)
    return results


# Обработка 100 элементов, но максимум 5 одновременно
asyncio.run(batch_process(range(1, 101), max_concurrent=5))
```

### Rate limiting для внешних API

```python
import asyncio
from datetime import datetime, timedelta
from typing import List


class RateLimiter:
    """Rate limiter для ограничения частоты запросов."""

    def __init__(self, max_requests: int, time_window: float):
        """
        Args:
            max_requests: Максимум запросов
            time_window: Временное окно в секундах
        """
        self.max_requests = max_requests
        self.time_window = time_window
        self.requests: List[float] = []
        self.lock = asyncio.Lock()

    async def acquire(self):
        """Получить разрешение на запрос."""
        async with self.lock:
            now = asyncio.get_event_loop().time()

            # Удаляем старые запросы
            cutoff = now - self.time_window
            self.requests = [req for req in self.requests if req > cutoff]

            # Проверяем лимит
            if len(self.requests) >= self.max_requests:
                # Ждем
                sleep_time = self.requests[0] + self.time_window - now
                await asyncio.sleep(sleep_time)
                return await self.acquire()

            # Добавляем текущий запрос
            self.requests.append(now)


# Использование
rate_limiter = RateLimiter(max_requests=10, time_window=1.0)


async def call_external_api(data):
    """Вызов внешнего API с rate limiting."""
    await rate_limiter.acquire()

    async with ClientSession() as session:
        async with session.post('https://api.example.com', json=data) as resp:
            return await resp.json()
```

## Типичные ошибки и как их избежать

### 1. Забыли await

```python
# ❌ ПЛОХО - забыли await
async def bad_example():
    result = fetch_user(1)  # Это coroutine object, не результат!
    print(result)  # <coroutine object fetch_user at 0x...>


# ✅ ХОРОШО
async def good_example():
    result = await fetch_user(1)
    print(result)  # {"id": 1, "name": "John"}
```

### 2. Blocking операции в async коде

```python
import time
import requests  # Синхронная библиотека!


# ❌ ПЛОХО - блокирующие операции
async def bad_sync_in_async():
    time.sleep(1)  # Блокирует event loop!
    response = requests.get('https://api.example.com')  # Блокирует!


# ✅ ХОРОШО - асинхронные операции
async def good_async():
    await asyncio.sleep(1)  # Не блокирует

    async with aiohttp.ClientSession() as session:
        async with session.get('https://api.example.com') as resp:
            return await resp.json()
```

**Если нужна синхронная библиотека:**

```python
import asyncio
from concurrent.futures import ThreadPoolExecutor


executor = ThreadPoolExecutor(max_workers=5)


async def run_sync_in_thread():
    """Запуск синхронной функции в отдельном потоке."""
    loop = asyncio.get_event_loop()

    # Запускаем в thread pool
    result = await loop.run_in_executor(
        executor,
        requests.get,  # Синхронная функция
        'https://api.example.com'
    )

    return result.json()
```

### 3. Создание tasks без await

```python
# ❌ ПЛОХО - task создан, но не awaited
async def bad_fire_and_forget():
    asyncio.create_task(process_data())  # Task потеряется!
    return "Done"


# ✅ ХОРОШО - сохраняем reference
async def good_task_management():
    task = asyncio.create_task(process_data())
    # Можем дождаться позже
    result = await task
    return result


# ✅ Или храним в списке
tasks = []

async def good_task_tracking():
    task = asyncio.create_task(process_data())
    tasks.append(task)  # Сохраняем reference
```

### 4. Exception handling в concurrent tasks

```python
# ❌ ПЛОХО - одна ошибка убивает все
async def bad_error_handling():
    await asyncio.gather(
        task1(),
        task2(),  # Если упадет - все упадет!
        task3(),
    )


# ✅ ХОРОШО - обрабатываем ошибки
async def good_error_handling():
    results = await asyncio.gather(
        task1(),
        task2(),
        task3(),
        return_exceptions=True  # Ошибки как результаты
    )

    for i, result in enumerate(results):
        if isinstance(result, Exception):
            print(f"Task {i} failed: {result}")
        else:
            print(f"Task {i} result: {result}")
```

## Best Practices

### 1. Используйте asyncio.create_task()

```python
# ❌ ПЛОХО - последовательное выполнение
async def sequential():
    result1 = await slow_operation1()
    result2 = await slow_operation2()
    return result1, result2


# ✅ ХОРОШО - параллельное выполнение
async def concurrent():
    task1 = asyncio.create_task(slow_operation1())
    task2 = asyncio.create_task(slow_operation2())
    return await task1, await task2
```

### 2. Настраивайте timeouts

```python
# ✅ Всегда используйте timeouts для внешних запросов
async def with_timeout():
    try:
        async with asyncio.timeout(5):
            return await external_api_call()
    except asyncio.TimeoutError:
        return None
```

### 3. Ограничивайте конкурентность

```python
# ✅ Используйте Semaphore для контроля
semaphore = asyncio.Semaphore(10)  # Максимум 10 одновременно

async def limited_task():
    async with semaphore:
        return await process()
```

### 4. Graceful shutdown

```python
# src/app.py
async def on_cleanup(app):
    """Graceful shutdown."""
    # Отменяем все активные tasks
    tasks = [t for t in asyncio.all_tasks() if not t.done()]

    for task in tasks:
        task.cancel()

    # Ждем завершения
    await asyncio.gather(*tasks, return_exceptions=True)
```

## Дополнительные материалы

### Документация
- [asyncio Documentation](https://docs.python.org/3/library/asyncio.html)
- [Real Python: Async IO](https://realpython.com/async-io-python/)
- [aiohttp Documentation](https://docs.aiohttp.org/)

### Статьи
- [asyncio Cheat Sheet](https://cheat.readthedocs.io/en/latest/python/asyncio.html)
- [Common asyncio Mistakes](https://xinhuang.github.io/posts/2017-07-31-common-mistakes-using-python3-asyncio.html)
- [Async Patterns in Python](https://www.roguelynn.com/words/asyncio-we-did-it-wrong/)

### Книги
- "Using Asyncio in Python" - Caleb Hattingh
- "Python Concurrency with asyncio" - Matthew Fowler

### Видео
- [asyncio: We Did It Wrong](https://www.youtube.com/watch?v=M-UcUs7IMIM)
- [Demystifying Python's Async](https://www.youtube.com/watch?v=iG6fr81xHKA)

## Следующая неделя

На [Неделе 10](../week-10/README.md) изучим Background jobs с Celery/arq! 🚀

---

**Удачи с асинхронными паттернами! ⚡**

