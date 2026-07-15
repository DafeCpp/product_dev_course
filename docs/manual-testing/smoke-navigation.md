# Сценарии: smoke-навигация по разделам

Быстрая проверка, что каждый раздел портала открывается без ошибок: корректный HTTP-статус при прямом заходе, чистая консоль, отрисовка контента.

**Общее предусловие:** стек поднят, авторизован как `admin` / `Admin123` (см. [`README.md`](README.md)).

---

### TC-NAV-01 — Все разделы открываются (прямой заход по URL)

Проверка HTTP-статуса при прямом GET на каждый роут (имитация перезагрузки / deep-link). Быстрый прогон:

```bash
for p in / /login /projects /experiments /sensors /sensor-monitor \
         /telemetry /webhooks /admin/users /admin/audit /admin/scripts; do
  printf "%s  %s\n" "$(curl -s -o /dev/null -w '%{http_code}' http://localhost:3000$p)" "$p"
done
```

| Роут | Ожидаемый статус |
|------|------------------|
| `/`, `/login` | 200 |
| `/experiments`, `/sensors`, `/sensor-monitor`, `/telemetry`, `/webhooks` | 200 |
| `/admin/users`, `/admin/audit`, `/admin/scripts` | 200 |
| `/projects` | 200 |

**Факт (2026-06-10):** ⚠️ Все роуты вернули `200`, **кроме `/projects` → HTTP 500**. См. [BUG-F-013](https://linear.app/lostpointer/issue/LOS-6/bug-f-013-projects-page-returns-http-500-on-direct-load-f5-reload). Прямой заход/перезагрузка страницы «Проекты» ломается; client-side навигация (клик по пункту меню) работает.

---

### TC-NAV-02 — Навигация по боковому меню (client-side)
**Предусловие:** авторизован, открыт любой приватный раздел.

| # | Шаг | Ожидаемый результат |
|---|-----|---------------------|
| 1 | Поочерёдно кликать пункты меню: Проекты, Эксперименты, Датчики, Монитор датчиков, Телеметрия, Webhooks, Пользователи, Аудит, Скрипты | URL меняется, контент раздела отрисовывается, без полной перезагрузки страницы |

**Факт (2026-06-10):** ✅ Переходы по ссылкам работают для всех разделов, включая `/projects` (в отличие от прямого захода — TC-NAV-01).

> ⚠️ Известная UI-особенность: при узком вьюпорте боковое меню уезжает за пределы экрана. Для надёжного клика по пунктам тестировать на ширине ≥ 1280px.

---

### TC-NAV-03 — Чистота консоли по разделам
**Предусловие:** авторизован.

| # | Шаг | Ожидаемый результат |
|---|-----|---------------------|
| 1 | Открыть каждый раздел, проверить консоль браузера | Нет ошибок уровня `error` (кроме осознанно ожидаемых, напр. `401 /auth/me` до логина) |

**Факт (2026-06-10):** ⚠️ Найдено:
- `/webhooks` → `400 Bad Request` на `GET /api/v1/webhooks` и `/api/v1/webhooks/deliveries` — запросы уходят без `project_id`. См. [BUG-F-014](https://linear.app/lostpointer/issue/LOS-7/bug-f-014-webhooks-page-sends-requests-without-project-id-400-bad). Воспроизводится на пустом воркспейсе (нет проектов).
- `/projects` (прямой заход) → `500` (следствие [BUG-F-013](https://linear.app/lostpointer/issue/LOS-6/bug-f-013-projects-page-returns-http-500-on-direct-load-f5-reload)).
- Остальные разделы — консоль чистая.

---

## Заметка по окружению

Portal в dev раздаётся **Vite dev-сервером** (`npm run dev`, не nginx). Часть путей проксируется на `auth-proxy` через `server.proxy` в `vite.config.ts`. Прокси-правило по «голому» префиксу `/projects` перехватывает одноимённый SPA-роут — корень [BUG-F-013](https://linear.app/lostpointer/issue/LOS-6/bug-f-013-projects-page-returns-http-500-on-direct-load-f5-reload).
