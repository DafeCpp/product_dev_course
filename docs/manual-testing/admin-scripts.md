# Сценарии: Admin — Скрипты (реестр и выполнение)

Раздел **Скрипты** (`/admin/scripts`): реестр управляющих скриптов, CRUD, запуск, история и
детали выполнения, отмена.

**Где реализовано:**
- Страница `pages/Scripts.tsx` + `pages/scripts/*` (`ScriptsTable`, `ScriptFormModal`,
  `ScriptExecuteModal`, `ScriptExecDetailModal`, `ExecutionsTable`); API `src/api/scripts.ts`.
- Скрипты: `GET/POST /api/v1/scripts`, `GET/PATCH/DELETE /api/v1/scripts/{id}`.
- Выполнение: `POST /api/v1/scripts/{id}/execute` (`parameters`, `target_instance`);
  `GET /api/v1/executions`, `GET /api/v1/executions/{id}`, `POST /api/v1/executions/{id}/cancel`.
- Статусы выполнения: Ожидание / Выполняется / Завершён / Ошибка / Таймаут / Отменён.
- Доступ к разделу: nav требует `scripts.manage` / `scripts.execute`.

**Предусловие:** авторизован как `admin` (`scripts.manage`).

---

### TC-SCRIPT-01 — Список и CRUD скрипта
**Предусловие:** открыт `/admin/scripts`.

| # | Шаг | Ожидаемый результат |
|---|-----|---------------------|
| 1 | Открыть раздел | `GET /api/v1/scripts` → `200`; таблица скриптов; консоль чистая |
| 2 | «Создать скрипт» (`ScriptFormModal`), сохранить | `POST /api/v1/scripts` → `201`; тост «Скрипт создан»; строка в таблице |
| 3 | Редактировать скрипт | `PATCH /api/v1/scripts/{id}` → `200` |
| 4 | Удалить скрипт | `DELETE /api/v1/scripts/{id}` → `200/204` |

---

### TC-SCRIPT-02 — Запуск скрипта
**Предусловие:** есть активный скрипт.

| # | Шаг | Ожидаемый результат |
|---|-----|---------------------|
| 1 | «Запустить» (`ScriptExecuteModal`), задать параметры/`target_instance` | `POST /api/v1/scripts/{id}/execute` → `200/202`; создано выполнение |
| 2 | Наблюдать статусы | Ожидание → Выполняется → Завершён (или Ошибка/Таймаут) |
| 3 | (негатив) Невалидные параметры | `400`, тост «Не удалось запустить скрипт» |

---

### TC-SCRIPT-03 — История и детали выполнения
**Предусловие:** есть ≥1 выполнение.

| # | Шаг | Ожидаемый результат |
|---|-----|---------------------|
| 1 | Открыть `ExecutionsTable` | `GET /api/v1/executions` → `200`; список с фильтром по `script_id`/`status` |
| 2 | Открыть детали (`ScriptExecDetailModal`) | `GET /api/v1/executions/{id}` → `200`; шаги/логи/итог |
| 3 | Отменить активное выполнение | `POST /api/v1/executions/{id}/cancel` → `200`; статус → Отменён |

---

### TC-SCRIPT-04 — RBAC (негатив)
| # | Шаг | Ожидаемый результат |
|---|-----|---------------------|
| 1 | Пользователь без прав открывает `/admin/scripts` | «Нет доступа»/`403`; пункт скрыт в nav |
| 2 | `POST /api/v1/scripts/{id}/execute` без `scripts.execute` | `403 Missing permission` |

---

## Не покрыто / на доработку
- Реальное выполнение на целевом инстансе (target_instance) в разных сервисах.
- Таймаут-поведение долгих скриптов и очистка «зависших» выполнений.

> Статус: сценарии описаны; результаты прогона — в [`test-reports.md`](test-reports.md).
