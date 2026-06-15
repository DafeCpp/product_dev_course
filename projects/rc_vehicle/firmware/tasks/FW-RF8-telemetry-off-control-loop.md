# FW-RF8 — Развязать телеметрию и управление (построение JSON вне control loop)

**Источник:** железная сессия 2026-06-13 + обсуждение 2026-06-14 (после FW-R13)
**Приоритет:** MEDIUM (надёжность реального времени)
**Статус:** [x] Исправлено (PR; host-тесты + сборка esp32s3 зелёные;
DIAG loop Hz — проверить на железе)
**Файлы:** `common/control_loop_processor.cpp` (`UpdateTelemetry`),
`common/control_components.cpp` (`TelemetryHandler::SendTelemetry`/`BuildTelemJson`),
`esp32_common/websocket_server.cpp`, `esp32_s3/main/vehicle_control_platform_esp32.cpp`

## Принцип

Телеметрия **не должна никак влиять на управление**. Контур управления — 500 Гц
(2 мс), любые задержки/джиттер/блокировки/аллокации в нём критичны.

## Проблема

Сейчас связанность остаётся: `TelemetryHandler::SendTelemetry` вызывается
**внутри control loop** (`control_loop_processor.cpp` → `UpdateTelemetry` →
`SendTelemetry`) и на каждом интервале (~20 Гц) строит JSON через cJSON
(`BuildTelemJson`) — это heap alloc/free **на потоке управления**.

Отправка по WebSocket уже вынесена в отдельный `telem_sender_task` (FW-R6), а
гейт client-count убран (FW-R13), но **построение JSON — всё ещё в горячем
пути**. Стоимость cJSON и фрагментация кучи бьют по 500 Гц циклу; в худшем
случае аллокатор под нагрузкой даёт всплеск латентности прямо в управлении.

Косвенный эффект уже наблюдался: на железной сессии баг телеметрии (FW-R13)
совпал с жалобой на «слабый руль» (FW-R12), хотя управление было исправно —
связанность телеметрии и контроля порождает и регрессы, и ложные диагнозы.

## Предлагаемое решение

1. Control loop публикует только **лёгкий POD-снапшот** (`TelemetrySnapshot`,
   уже существует) в очередь/двойной буфер — без аллокаций, фиксированный
   размер, `xQueueOverwrite` (как телеметрия — «последнее состояние важнее
   истории»).
2. Построение JSON (`BuildTelemJson`) + сериализация + отправка переезжают
   **целиком** в `telem_sender_task` (или отдельную задачу телеметрии),
   желательно на другом ядре (control loop — Core 1, телеметрия — Core 0).
3. На host (GTest) поведение `TelemetryHandler` сохранить тестируемым: вынести
   построение JSON в чистую функцию от снапшота (без зависимости от потока).

Итог: в control loop по телеметрии остаётся только запись POD-снапшота в
очередь (детерминированно, без кучи). Любая стоимость/сбой телеметрии физически
не достигает 500 Гц.

## Объём работ

- [x] `TelemetryHandler::SendTelemetry` — только гейт по частоте +
      `platform_.PublishTelem(snapshot)`, без `BuildTelemJson`
- [x] `BuildTelemJson` вынесена в свободную чистую функцию; вызывается в
      `telem_sender_task` (потребитель очереди, Core 0, низкий приоритет)
- [x] Снапшот — POD без динамики; `static_assert(is_trivially_copyable)`
      фиксирует это; очередь FreeRTOS копирует снимок (`xQueueOverwrite`)
- [ ] Замер `DIAG: loop=… Hz` и джиттера до/после на железе (не хуже)
- [x] Host-тесты `test_telemetry_handler.cpp` зелёные (JSON-функция чистая)

## Реализация

Поток до: control loop → `SendTelemetry` → `BuildTelemJson` (cJSON heap **на
потоке управления**) → `SendTelem(json)` → очередь JSON → `telem_sender_task`.

Поток после: control loop → `SendTelemetry` → `PublishTelem(snapshot)` →
очередь POD-снимков (`xQueueOverwrite`, memcpy) → `telem_sender_task`
**строит JSON** (`BuildTelemJson`) и шлёт по WS.

- `TelemetrySnapshot` получил поле `failsafe` (раньше `BuildTelemJson` читал
  `platform_.FailsafeIsActive()` — теперь снимок самодостаточен, JSON строится
  без обращения к платформе из чужого потока). Поле заполняет `UpdateTelemetry`.
- Платформенный интерфейс: `SendTelem(string_view)` → `PublishTelem(const
  TelemetrySnapshot&)`. Очередь в `websocket_server.cpp` теперь несёт снимок,
  а не JSON-буфер 2 КБ.
- `BuildTelemJson` — свободная функция от снимка (host-тестируемая).

## Критерии приёмки

- [x] В control loop нет постройки JSON и heap-аллокаций ради телеметрии
      (только запись POD-снимка в очередь)
- [ ] `loop Hz` и джиттер на железе не деградировали (DIAG до/после)
- [x] При полностью отключённой/сломанной телеметрии управление не меняется
      (телеметрия физически за очередью, в другом потоке/ядре)

## Связанные

- **FW-R6** — вынос отправки WS в `telem_sender_task` (предшественник).
- **FW-R13** — убран гейт client-count (убрал обратную связанность; RF8 —
  про прямую: control loop тратит время на телеметрию).
- Перекликается с планом бинарного эндпоинта скачивания телеметрии (тоже
  вынос тяжёлой работы с телеметрией из горячего пути).
