# FW-R13 — Web UI: нет связи с контроллером, нет телеметрии и графиков

**Источник:** железная сессия 2026-06-13 (тест ветки test/wave-4-and-rf1)
**Приоритет:** HIGH (наблюдаемость через Web UI не работает)
**Статус:** [x] Исправлено (подтверждено на железе 2026-06-14)
**Файлы:** `common/control_components.cpp` (фикс), тесты
`tests/unit/test_telemetry_handler.cpp`

## Корень (подтверждён на железе)

Диагностическая прошивка (ветка `test/fw-r12-r13-diagnostics`, ворота обойдены
+ логи) дала однозначную картину:

```
TELEM-DIAG clients=0 gate_would=BLOCK          ← до подключения
WS-DIAG httpd_total=2 ws_matched=0 sent_ok=0    ← страница есть, WS ещё нет
WS-DIAG httpd_total=3 ws_matched=1 sent_ok=1    ← WS открылся, кадры пошли
telem_sender: 199 frames sent in 10s, clients=3 (~20 Гц)
```

С обойдёнными воротами телеметрия идёт идеально (`ws_matched=1, sent_ok=1,
sent_fail=0`). Значит весь конвейер исправен, а блокировал ровно гейт
`if (GetWebSocketClientCount()==0) return;` в `SendTelemetry`.

**Причина:** циклическая зависимость. Счётчик клиентов обновляется в основном
из самого пути отправки (`WebSocketSendTelem` через `httpd_get_client_list`),
а единственный bootstrap (`ws_handler` HTTP_GET handshake) на ESP-IDF v6.0
срабатывает ненадёжно. При `count==0` телеметрия не ставится в очередь →
`WebSocketSendTelem` не вызывается → `count` остаётся 0. Самоисцеления нет.

## Решение

Убрать гейт из `TelemetryHandler::SendTelemetry`: решение о доставке принимает
транспорт (`WebSocketSendTelem` уже шлёт кадры только реальным WS-fd; если их
нет — никому). Постройка JSON на 20 Гц без слушателей по стоимости ничтожна.

## Симптом

В Web UI:
- не показывается связь с контроллером (бейдж «Нет связи»),
- не отображается телеметрия,
- не строятся графики.

При этом по серийнику телеметрия/DIAG идёт (IMU работает на 500 Гц), т.е.
данные есть — не доходят именно до Web UI по WebSocket.

## Как устроен путь телеметрии

1. `control_loop_processor.cpp:192` → `TelemetryHandler::SendTelemetry`.
2. `control_components.cpp:215-227` — `SendTelemetry`:
   **`if (platform_.GetWebSocketClientCount() == 0) return;`** (строка ~222),
   иначе `BuildTelemJson` → `platform_.SendTelem(json)`.
3. `vehicle_control_platform_esp32.cpp:264` — `SendTelem` →
   `WebSocketEnqueueTelem(buffer)`.
4. `websocket_server.cpp` — `s_telem_queue` (xQueueOverwrite) →
   `telem_sender_task` рассылает клиентам (рефактор FW-R6).
5. Клиент: `web/app.js:8` `ws://${hostname}/ws`, `onmessage` →
   `data.type==='telem'` → `updateTelem`; статус контроллера —
   `mcu_pong_ok` (всегда `true` в JSON) **или** факт прихода телеметрии.

## Кандидаты на причину

1. **`GetWebSocketClientCount()` возвращает 0**, хотя браузер подключён →
   телеметрия не ставится в очередь вовсе. Тогда «нет связи + нет телеметрии
   + нет графиков» объясняется одной точкой. Проверить учёт клиентов в
   `websocket_server.cpp` (регистрация fd при WS-handshake, очистка при
   закрытии) — возможный регресс после FW-R6 (новая очередь/таск).
2. **WS не открывается**: handshake `/ws` падает, или клиент стучится не по
   тому хосту (открыли UI по STA-IP, а WS пытается на другой). Проверить в
   DevTools браузера статус WS и `telemRxCount` (`rx:` счётчик в UI).
3. **`telem_sender_task` не стартовал** (нехватка кучи при `xTaskCreate`,
   `websocket_server.cpp:174`) — тогда очередь наполняется, но никто не шлёт.
4. **Поломка JSON телеметрии** (после RF1/RF3/R3) → `JSON.parse` бросает,
   `onmessage` падает в `catch` (app.js:231) — но тогда был бы `rx>0` и
   ошибка в `telem-data`. Проверить по UI.

## Как диагностировать

- DevTools браузера → Network → WS: открылся ли `/ws`, идут ли фреймы.
- В UI смотреть счётчик `rx:` (`telem-counter`): 0 → телеметрия не доходит
  (причины 1-3); растёт, но пусто → парсинг/формат (причина 4).
- Серийник: лог `telem_sender: N frames sent in 10s, clients=K`
  (`websocket_server.cpp:63`) — `clients=0` подтверждает причину 1.

## Критерии приёмки

- [x] При подключённом браузере в Web UI идёт телеметрия, строятся графики,
      бейдж контроллера — «Подключено» (подтверждено на железе 2026-06-14)
- [x] Лог `telem_sender: ... clients=K` показывает K>0 при открытом UI
      (`clients=3`, `ws_matched=1`, `sent_ok=1`)
- [x] Найдена точка обрыва (гейт client-count в `SendTelemetry`) и устранена
