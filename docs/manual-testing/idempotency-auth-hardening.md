# Сценарии: идемпотентность и hardening аутентификации

По теме ветки `fix/backend-idempotency-and-auth-hardening`. Проверяют поведение `Idempotency-Key`, single-use инвайтов, admin-reset пароля и устойчивость парсинга токенов.

**Где реализовано:**
- Idempotency: заголовок `Idempotency-Key` на `POST` создания в experiment-service — experiments, runs, sensors, capture-sessions (`services/idempotency.py`, `IDEMPOTENCY_HEADER`). Конфликт → **HTTP 409** (`IdempotencyConflictError`).
- Auth hardening: `auth_service/api/routes/auth.py` — invites (`/auth/admin/invites`), admin-reset (`/auth/admin/users/{user_id}/reset`), парсинг токенов.

**Предусловие:** стек поднят; для API-сценариев нужен валидный access-токен/сессионные cookie (логин `admin`/`Admin123`).

---

### TC-IDEM-01 — Повтор POST с тем же ключом и телом → один объект
**Предусловие:** есть активный проект; готов `POST /api/v1/experiments` с заголовком `Idempotency-Key: <uuid>`.

| # | Шаг | Ожидаемый результат |
|---|-----|---------------------|
| 1 | Отправить создание эксперимента с `Idempotency-Key: K1`, телом B1 | `201`/`200`, создан объект, запомнить `id` |
| 2 | Повторить идентичный запрос (тот же `K1`, то же тело B1) | **Тот же ответ** (тот же `id`), второй объект НЕ создан (replay из кэша) |
| 3 | Проверить в БД количество эксперид с этим именем | Ровно 1 запись |

---

### TC-IDEM-02 — Тот же ключ, другое тело → 409 (негативный)
**Предусловие:** ключ `K1` уже использован (TC-IDEM-01).

| # | Шаг | Ожидаемый результат |
|---|-----|---------------------|
| 1 | `POST` с `Idempotency-Key: K1`, но изменённым телом B2 | **HTTP 409** «Idempotency key reused with different payload»; новый объект не создан |

---

### TC-IDEM-03 — Конкурентные запросы с одним ключом (race)
**Предусловие:** скрипт, отправляющий 2 параллельных идентичных `POST` с одним `Idempotency-Key: K2`.

| # | Шаг | Ожидаемый результат |
|---|-----|---------------------|
| 1 | Запустить два запроса одновременно | Один создаёт объект; второй либо ждёт и получает тот же результат, либо `409` «key belongs to another request». **Дублей не создаётся** |
| 2 | Проверить БД | Ровно 1 объект (см. фикс `feb640e` — concurrency divergence) |

```bash
# пример параллельного запуска (подставить cookie/headers и project_id)
KEY=$(cat /proc/sys/kernel/random/uuid)
for i in 1 2; do
  curl -s -o /dev/null -w "%{http_code}\n" -X POST http://localhost:8080/api/v1/experiments \
    -b /tmp/cj.txt -H "X-CSRF-Token: <csrf>" -H "Idempotency-Key: $KEY" \
    -H 'Content-Type: application/json' \
    -d '{"project_id":"<PID>","name":"idem-race"}' &
done; wait
```

---

### TC-AUTHH-01 — admin-reset пароля пользователя
**Предусловие:** авторизован как `admin`; есть целевой пользователь.

| # | Шаг | Ожидаемый результат |
|---|-----|---------------------|
| 1 | `POST /auth/admin/users/{user_id}/reset` с заданным паролем | Пароль сброшен; ответ ок; (опц.) выставлен `password_change_required` |
| 2 | `POST .../reset` без пароля (авто-генерация) | Возвращается сгенерированный пароль out-of-band |
| 3 | Войти под пользователем новым паролем | Успешно; при `password_change_required` — редирект на `/change-password` |
| 4 | Старые refresh-токены пользователя после reset | Инвалидированы (нельзя обновить сессию старым токеном) |

---

### TC-AUTHH-02 — Single-use инвайт
**Предусловие:** создан инвайт-токен.

| # | Шаг | Ожидаемый результат |
|---|-----|---------------------|
| 1 | Зарегистрироваться по инвайту | Успех, инвайт помечен использованным |
| 2 | Повторно зарегистрироваться по тому же токену | **Отклонено** (single-use); понятная ошибка, не 500 |
| 3 | Использовать просроченный/отозванный (`DELETE /auth/admin/invites/{token}`) инвайт | Отклонено |

---

### TC-AUTHH-03 — Устойчивость парсинга токенов (негативный, hardening)
**Предусловие:** доступен auth-proxy/auth-service.

| # | Шаг | Ожидаемый результат |
|---|-----|---------------------|
| 1 | Запрос с `Authorization: Bearer` без значения / мусорной строкой / не-JWT | **401**, не `500` (см. фикс `dfb1797` token parsing и `ae7fe0a` в telemetry-ingest) |
| 2 | Запрос с просроченным/подделанным JWT | `401`, доступ запрещён |
| 3 | `GET /auth/me` без cookie | `401` (ожидаемо), приложение редиректит на `/login` |

---

## Не покрыто / на доработку
- Idempotency на sensors и capture-sessions (аналогично TC-IDEM-01/02).
- TTL/срок жизни idempotency-записей (повтор после истечения).
- Полная ротация refresh-token family (см. ADR-004) — частично в backend integration-тестах.

> Статус: сценарии **описаны, ещё не прогонялись** вручную. Многие здесь удобнее автоматизировать (см. backend integration-тесты в `auth-service/tests/` и `experiment-service/tests/`). Результаты — в [`test-reports.md`](test-reports.md).
