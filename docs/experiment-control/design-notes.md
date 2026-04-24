# Design Notes — Experiment Control

Разведочные заметки по архитектуре подсистемы управления экспериментом.
Служат подготовкой к будущему RFC-0004.

---

## 1. Постановка задачи

Помимо наблюдения (телеметрия, алерты) в экспериментах требуется **активно
воздействовать** на стенд — открывать клапаны, менять обороты вентилятора,
включать нагреватель. Подсистема управления должна покрывать:

- **Manual (open-loop):** оператор нажимает кнопку в UI → команда уходит на
  исполнительное устройство.
- **Closed-loop:** автоматическая реакция на показания датчиков по заранее
  заданным правилам («если давление > X — открыть клапан»).

Ключевой вопрос — **производительность контура управления**. Частота
управления определяется физикой процесса: от долей герца (нагрев) до сотен
герц (вибрация, аэродинамика).

---

## 2. Latency-бюджет

Правило: каждое звено замкнутого контура ≤ ~20% периода, иначе фазовый
запас теряется и контур склонен к автоколебаниям.

| Частота | Период | Per-hop | Реалистично на Python/HTTP |
|---------|--------|---------|-----------------------------|
| 1 Гц | 1000 мс | 200 мс | ✅ легко |
| 10 Гц | 100 мс | 20 мс | ⚠️ предел |
| 50 Гц | 20 мс | 4 мс | ❌ нельзя |
| 100 Гц | 10 мс | 2 мс | ❌ только edge |
| 500 Гц | 2 мс | 0.4 мс | ❌ только MCU |

Текущий путь «датчик → ingest → БД → experiment-service → WS обратно →
устройство» — 50–200 мс в лучшем случае. **Потолок ≈ 5 Гц**. Для частот
выше нужен другой подход.

---

## 3. Варианты архитектуры

### Вариант ① — Edge control на устройстве

Сервер раздаёт **setpoint + параметры регулятора** при старте capture session.
Устройство исполняет PID/релейку локально. Сервер только мониторит и
медленно (≤ 1 Гц) корректирует setpoint.

- Петля управления: 0–2 мс на MCU.
- **Плюсы:** надёжно при потере связи (watchdog на устройстве), не зависит
  от стека backend.
- **Минусы:** сложнее для самодельных стендов без собственного контроллера.
  Логика дублируется (firmware + UI).
- **Прецедент:** RC Vehicle firmware (500 Гц).

### Вариант ② — Hybrid (split-brain)

Два контура:
- **Внутренний** (на устройстве, 100–500 Гц): держит текущий setpoint.
- **Внешний** (на сервере, 0.1–5 Гц): оптимизирует setpoint (adaptive tuning,
  модельное управление).

Сервер может быть на **обычном Python-стеке** — 5 Гц Python делает без
проблем. Это 90% промышленных применений.

### Вариант ③ — Dedicated control-service (Python, in-memory state)

Если устройство «тупое» (только датчики + исполнитель), нужен контур до
~30 Гц:

- Новый сервис с WebSocket к устройству (без auth-proxy на hot path —
  auth только на handshake).
- Live stream от telemetry-ingest через Redis pub/sub или RabbitMQ stream.
- Правила и PID state в памяти. Persistence ленивая.
- Оптимизации: `uvloop`, MessagePack/CBOR вместо JSON.

**Плюсы:** в рамках существующего стека.
**Минусы:** Python GIL и GC дают jitter 1–10 мс. Потолок ~50 Гц, 100 Гц
уже рискованно.

### Вариант ④ — Real-time backend (Rust/Go/C++)

Для гарантированных 50–200 Гц на сервере:

- Control-service на Rust/Go (низкий GC jitter) или C++ (детерминированный).
- Binary WebSocket или UDP.
- Lock-free очереди, NATS/Aeron вместо RabbitMQ.

**Минусы:** новый стек в зоопарке, сложная отладка. **Скорее всего не
нужно** — то же даёт вариант ② за меньшую цену.

### Вариант ⑤ — Real-time protocol (EtherCAT / OPC-UA PubSub)

Индустриальный стандарт. 1 kHz+, jitter < 100 мкс. Требует специализированных
сетевых стеков и промышленных ПЛК. Актуально только при интеграции с
готовым заводским оборудованием.

---

## 4. Рекомендуемый паттерн — Control Gateway

Обобщение вариантов ①–③ в единый паттерн: **SCADA-style "Field Gateway /
Supervisor split"**. Hot path замкнут локально в Gateway, backend живёт
в slow path (мониторинг + конфигурация).

### Схема

```
┌─ Стенд (физический) ─────────────────────────────┐
│                                                   │
│  Sensors ──► Control Gateway ──► Actuators        │
│              (fast loop, local)                   │
│                  │      ▲                         │
└──────────────────┼──────┼─────────────────────────┘
                   │      │
          (async, slow)  (async, config pull/push)
                   │      │
          ┌────────▼──┐  ┌┴──────────────────────┐
          │ telemetry │  │ experiment-service    │
          │   -ingest │  │ (или control-service) │
          └───────────┘  └───────────────────────┘
                            ▲
                            │  manual commands, setpoints, rules
                            │
                          UI (оператор)
```

### Ключевые свойства

1. **Control latency не зависит от backend.** Python стек может тормозить
   как угодно — петля работает. Все hops — по локальной шине.
2. **Failsafe by design.** При потере связи с backend Gateway работает по
   последней известной конфигурации. Watchdog обеспечивает safe state.
3. **Backend остаётся простым.** `experiment-service` отдаёт конфигурацию
   через REST, 5 Гц ему хватает.
4. **Горизонтальное масштабирование.** N стендов = N независимых Gateway.
5. **Переиспользование паттерна RC Vehicle.** Распространяем уже применённый
   подход на лабораторные стенды.

### Варианты реализации Gateway

| # | Хост | Стек | Частота | Когда |
|---|------|------|---------|-------|
| a | Raspberry Pi / industrial PC рядом со стендом | Python + asyncio | 10–50 Гц | Лабораторные стенды, Ethernet-датчики |
| b | То же | Rust/Go | 50–500 Гц | Если (a) не тянет |
| c | MCU (как RC Vehicle) | C++ / Rust | 500–2000 Гц | Сенсоры и исполнители на одной плате |
| d | Хост датчика / ноут оператора | Python | 10–30 Гц | Dev / прототип |

### Что внутри Gateway

- Драйверы железа (Modbus, CAN, SPI, UART, UDP — специфично под стенд).
- Rule engine (PID / релейка / FSM).
- Ring-buffer телеметрии → отправка в `telemetry-ingest` пакетами, не на
  hot path.
- HTTP/WS клиент к backend для pull конфигурации и получения manual
  commands.

### Протокол Gateway ↔ Backend

**Config (backend → Gateway, slow):**
```http
GET /api/v1/control/config?rig_id=X
```
```json
{
  "version": 42,
  "setpoints": { "pressure": 5.2 },
  "rules": [
    { "id": "r1", "if": "temp > 80", "then": "valve.close()", "hysteresis": 2 }
  ],
  "pid": { "kp": 2.0, "ki": 0.1, "kd": 0.05 },
  "sensors": [{ "id": "s1", "signal": "pressure", "driver": "modbus://0x01" }],
  "actuators": [{ "id": "v1", "driver": "modbus://0x20" }]
}
```

**Manual commands (UI → Gateway, interactive):**
```http
POST /api/v1/control/{rig_id}/command
{ "target": "v1", "action": "open", "value": 0.5 }
```
Доставка: WebSocket back-channel (< 100 мс), SSE/long-polling (~1–2 с), или
RabbitMQ topic (Gateway подписан).

**Telemetry (Gateway → ingest, существующий API):**
Gateway выглядит как обычный датчик. Используется существующий WebSocket
ingest (500 Гц уже подтверждено RC Vehicle). Важно: disk spool на стороне
Gateway — при потере связи буферизуем, догоняем потом, петля не страдает.

**Events (Gateway → backend, критичные):**
```http
POST /api/v1/control/events
```
Сработало правило, сработал auto-stop, превышен предел. Для audit log и
webhook.

### Backend: где живёт логика

#### Вариант A — расширить `experiment-service` (простой путь)
- Endpoints `/api/v1/control/*`.
- Новые таблицы: `rigs`, `control_configs`, `control_events`.
- Reuse существующей auth/RBAC/audit инфраструктуры.
- **Когда:** для MVP, если control должен следовать тем же permission-модели,
  что и эксперименты.

#### Вариант B — новый `control-service`
- Отдельный микросервис, подписан на события `experiment-service`
  (run started → активировать config).
- Изоляция: падение не ломает CRUD экспериментов.
- **Когда:** если control-функциональность разрастётся (rule versioning,
  approval flow, закрытые тесты).

**Рекомендация:** начать с A, вынести в B позже по мере роста.

### Как это упрощает RFC-0004

Паттерн **разбивает RFC-0004 на два независимых трека**:

1. **RFC-0004a — Gateway Protocol & Reference Implementation**
   - Протокол Gateway ↔ backend (config, commands, events).
   - Reference Python Gateway (пример для Linux-хоста).
   - SDK / документация, как писать Gateway для своего стенда.
   - **Не касается** rule engine.

2. **RFC-0004b — Control API & UI** (в backend)
   - Endpoints `/api/v1/control/*`.
   - UI для ручных команд и задания setpoint.
   - Переиспользует RFC-0003 rules для автоматизации.

Между ними нет тесной зависимости — можно разрабатывать параллельно.

---

## 5. Open questions

Нужно ответить до финализации RFC-0004:

1. **Use cases > 10 Гц.** Какие есть в обозримых экспериментах? Если нет —
   фиксируем 10 Гц как потолок и идём по варианту ③. Если есть — обязательно
   edge-Gateway.
2. **Готовы ли стенды иметь MCU/ПЛК?** Если да — edge-control становится
   дефолтом.
3. **Гарантия доставки команд.** At-least-once с ack или best-effort? Для
   fast control обычно last-message-wins (новая команда перекрывает старую).
4. **Failsafe.** Где живёт — на устройстве (watchdog, safe state) или на
   сервере? Edge надёжнее.
5. **Поставка Gateway.** Reference implementation (Python) или только
   спецификация + SDK? Если SDK — на каких языках (Python + C++ минимум)?
6. **Discovery.** Как Gateway находит backend — hardcoded URL, DNS, mDNS,
   через конфиг от планшета оператора?
7. **Auth Gateway.** Отдельный тип токена как у датчиков или полноценный
   service account с permissions?
8. **Config versioning.** Как откатывать плохую конфигурацию? Gateway
   хранит last-known-good?
9. **Offline-first.** Сколько секунд/минут/часов без backend считается
   приемлемым до перехода в safe state?
10. **Gateway = отдельный тип ресурса в платформе?** Вводим ли сущность
    `rig` / `gateway` как first-class в API (с owner, project, permissions)?

---

## 6. Следующие шаги

Порядок проработки:

1. **Провести discovery по use cases** — ответить на Q1/Q2 (нужны ли > 10 Гц,
   будут ли стенды с MCU). Решение определяет, идём по варианту ③ в backend
   или сразу edge-Gateway.
2. **Прототип reference Python Gateway** — взять один модельный стенд
   (виртуальный или реальный), пройти полный путь: config → petля → telemetry
   → manual command. Цель — почувствовать все hops и latency.
3. **Специфицировать протокол** — config format, commands format, auth,
   discovery. Черновик → в этот файл, финал → в RFC-0004a.
4. **API в backend (вариант A)** — MVP endpoints `/api/v1/control/*`, таблицы,
   базовый UI.
5. **Финализировать RFC-0004a и RFC-0004b**, открыть PR в `docs/RFC/`.
