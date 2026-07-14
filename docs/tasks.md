# Идеи и задачи на проработку

Черновой backlog идей, требующих дизайна и RFC перед реализацией.
**Все конкретные задачи ведутся в [Linear](https://linear.app/lostpointer).**

Дополнительно: [tasks-rbac-scripts.md](tasks-rbac-scripts.md) и
[experiment-tracking-status-and-roadmap.md](experiment-tracking-status-and-roadmap.md).

---

## RFC Issues in Linear

### Computed Channels & Data Processing

- **[LOS-138](https://linear.app/lostpointer/issue/LOS-138)** — RFC: Вычисляемые/производные величины (computed channels)
  - Вычисление сложных физических величин из показаний нескольких датчиков
  - Арифметические формулы, агрегации по окну, DSL для скриптов
  - Архитектурные вопросы: где считать, как хранить, обратная совместимость

### Observability & Alerts

- **[LOS-139](https://linear.app/lostpointer/issue/LOS-139)** — RFC: Алерты по значениям датчиков
  - Пороговые значения для давления, температуры, вибраций с цветовой индикацией
  - Backend: таблица `sensor_thresholds`, движок событий
  - Frontend: виджеты (лампочки, gauge, цифровые индикаторы), звуковые сигналы, dashboard-конструктор
  - Зависит от: LOS-138 (пороги часто ставят на вычисляемые величины)

### Experiment Control (Closed-loop)

- **[LOS-66](https://linear.app/lostpointer/issue/LOS-66)** — [EPIC] RFC-0004: Experiment control
  - Автоматическое управление стендом: открыть/закрыть клапан, изменить обороты, включить нагреватель
  - Варианты: open-loop (ручные команды из UI) vs closed-loop (автоматические правила)
  - Ключевые риски: latency, фазовая задержка, failsafe, sandbox для пользовательских правил
  - Применима для медленных процессов (температура, давление); не в scope для быстрой динамики (500 Гц RC Vehicle)

### Performance & Load Testing

- **[LOS-44](https://linear.app/lostpointer/issue/LOS-44)** (Done) — [EPIC] Rate limiting и нагрузочное тестирование телеметрии
- **[LOS-115](https://linear.app/lostpointer/issue/LOS-115)** (Backlog) — telemetry-ingest: инструментарий нагрузочного тестирования
  - k6-сценарии (ingest, API, SSE, export)
  - Seed-скрипт для предзаполнения БД
  - Makefile-цель, регулярный запуск (CI/CD), baseline + regression gates
  - Целевые метрики: P95 API < 400 мс при 200 RPS, 200 датчиков, 5k точек/сек
