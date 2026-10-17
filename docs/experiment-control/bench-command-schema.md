# Командный тракт bench-controller: JSON-схема снимка и команд (Ф2)

Контракт между прошивкой (`projects/bench_controller/firmware/common/`) и
веб-страницей отладки (Ф4, LOS-187). Тот же тракт позже принимает команды из
UART-линка STM32 (LOS-181) и с экрана (LOS-180). Семантика останова и
идемпотентности — P-1…P-5 из [safety-review](safety-review.md); полный
supervisory-протокол — LOS-74.

Версия схемы: `"v": 1`. Новое поле — совместимое расширение, смена смысла
существующего — повышение `v`.

## Общая картина

```
веб / UART / экран ──Post──► ICommandInbox ──TryTake (начало тика)──► BenchControlLoop
                                                                         │ Execute: идемпотентность,
                                                                         │ блокировки, лимиты, журнал
веб / UART / экран ◄── JSON ◄── ISnapshotPublisher ◄── Publish (25 Гц) ◄─┘
```

- Очередь — single-slot overwrite: если вторая команда пришла раньше, чем
  контур забрал первую, первая теряется (`counters.cmd_overwritten`).
  **Клиент не шлёт следующую команду, пока не получил ack на предыдущую.**
- Снимок публикуется раз в `publish_every_ticks` (по умолчанию 20 тиков, 25 Гц).
  Ack приходят внутри снимка (`acks`, последние 4), отдельного канала нет.
- JSON строится вне контура 500 Гц (`snapshot_json.hpp`): в hot path нет
  аллокаций и форматирования.

## Команды (клиент → прошивка)

Плоский JSON-объект, строгий набор ключей (неизвестный ключ — ошибка).
`cmd_id` — целое `1…4294967295`, уникальное для каждой новой команды; `0`
зарезервирован.

| `type` | Аргументы | Смысл |
|---|---|---|
| `enable` | `enable`: bool | Разрешить/запретить работу канала |
| `set_mode` | `mode`: `"force"` \| `"displacement"` | Сменить режим (bumpless); уставка берётся равной текущему значению |
| `set_setpoint` | `value`: число | Ручная уставка в единицах активного режима (Н или мм) |
| `controlled_stop` | — | P-1: остановить программу и удерживать позицию |
| `e_stop_soft` | — | P-2: программно снять enable и удерживать позицию. **Не средство защиты людей**, в UI подписывается «stop (soft)» |

```json
{"cmd_id": 7, "type": "set_setpoint", "value": 12.5}
{"cmd_id": 8, "type": "enable", "enable": true}
{"cmd_id": 9, "type": "set_mode", "mode": "force"}
{"cmd_id": 10, "type": "controlled_stop"}
{"cmd_id": 11, "type": "e_stop_soft"}
```

Разбор на стороне прошивки: `ParseCommandJson` (коды `ParseError` вернёт
транспорт клиенту сам; в контур такая команда не попадает).

### Правила исполнения

1. **Идемпотентность.** Повтор `cmd_id` из последних 16 принятых возвращает
   исходный ack с `duplicate: true` и не исполняется заново, даже если
   содержимое другое. Отклонённые команды тоже запоминаются (повтор — снова
   nack). `cmd_id: 0` отклоняется и не запоминается.
2. **Не поверх программы.** Пока идёт программа нагружения, `enable(true)`,
   `set_mode` и `set_setpoint` отклоняются (`program_running`).
   `controlled_stop`, `e_stop_soft` и `enable(false)` проходят всегда.
3. **Удержание (`holding`).** После останова, потери связи или разрушения
   образца контур держит позицию в displacement-режиме.
   - Операторский останов (`controlled_stop`, `e_stop_soft`): `set_mode` и
     `set_setpoint` дают `stopped`; `enable(true)` возобновляет ручное
     управление с текущей позиции.
   - Защитное удержание (`link_safe_hold`, `specimen_failure`): `set_mode`,
     `set_setpoint` и `enable(true)` дают `held`; сброс — вне этого
     тракта. Защитная причина перекрывает операторскую.
4. **Лимиты.** `set_setpoint` вне `±force_limit_n` / `±position_limit_mm`
   активного режима или не число — `out_of_range` (отклоняется, не
   обрезается). Принятая уставка отрабатывается рампой
   (`force_ramp_n_per_s`, `position_ramp_mm_per_s`).

## Ack (внутри снимка, также `BuildAckJson`)

```json
{"cmd_id": 7, "cmd": "set_setpoint", "result": "accepted", "reason": "none", "duplicate": false}
```

`result`: `accepted` | `rejected`.
`reason`: `none` | `invalid_id` | `program_running` | `held` | `stopped` | `out_of_range`.

## Снимок состояния (прошивка → клиент)

```json
{
  "v": 1, "type": "snapshot", "seq": 1201, "uptime_ms": 48040,
  "mode": "force",                  // "force" | "displacement"
  "enabled": true,
  "link_state": "running",          // "running" | "grace" | "ramp_down" | "safe_hold"
  "holding": false,
  "stop_reason": "none",            // последняя причина удержания, см. ниже
  "program_active": false,
  "failure_latched": false,
  "estop_chain": "unknown",         // "unknown" | "ok" | "open" (P-4)
  "setpoint": {"program": 0.0, "commanded": 12.5, "ramped": 9.1, "effective": 9.0},
  "valve_command": 0.213,           // [-1..1]
  "feedback": {"force_n": 8990.2, "position_mm": 3.1, "status": 0, "fresh": true, "age_ticks": 0},
  "counters": {"cmd_total": 5, "cmd_rejected": 1, "cmd_duplicate": 1, "cmd_overwritten": 0},
  "acks": [ {…ack…}, … ],           // до 4 последних, от старых к новым
  "tick_us": {"count": 24000, "min": 1990, "avg": 2000, "p99": 2010, "max": 2150},
  "journal_total": 12,
  "journal": [ {…событие…}, … ]     // до 16 последних, от старых к новым
}
```

- Любое число может прийти как `null` (NaN/inf в JSON не передаются).
- `stop_reason`: `none` | `operator_controlled_stop` | `operator_e_stop_soft` |
  `link_safe_hold` | `specimen_failure`.
- `estop_chain: "unknown"` — у платформы нет источника сигнала; это не «ok».
- `tick_us` пересчитывается раз в `stats_refresh_ticks` (по умолчанию 1 с).
- `seq` растёт на каждую публикацию; по пропуску `seq` клиент видит потерю
  снимков.

### События журнала

Поля `seq` (сквозной номер с 0) и `t_ms` (время от старта) есть у всех;
остальные зависят от `kind`:

| `kind` | Дополнительные поля |
|---|---|
| `command` | `cmd_id`, `cmd`, `result`, `reason`, `duplicate` |
| `link_state` | `from`, `to` (значения `link_state`) |
| `failure_latch` | — |
| `hold` | `reason` (значения `stop_reason`) |

Журнал пишет **каждую** команду (принятую, отклонённую и дубль). Событие
`hold` применяется во время исполнения команды и пишется раньше записи
самой команды. В контуре хранится 64 события; во внешний снимок попадают
последние 16 (клиент дедуплицирует по `seq`). Локальный NVS-спул —
`JournalSpool` через `BenchPlatform::PersistJournalEntry`, вызывается из
низкоприоритетной задачи по опубликованным снимкам, не из контура.

## Что здесь не делается

- `ramp_ms` у `controlled_stop` (P-1 в LOS-74): плавность задаёт
  `capture_ramp_s` контура; параметр появится вместе с протоколом LOS-74.
- Сброс защитного удержания и возобновление программы.
- Реальное заполнение полей `bus` и `info` (см. ниже): прошивка Ф3/A-1
  добавит их как расширение схемы; страница отладки и mock-WS уже
  читают их.
- FreeRTOS-реализации `ICommandInbox`/`ISnapshotPublisher` — платформенный
  слой A-1 (LOS-188); для host есть `LockedCommandSlot` и
  `LockedSnapshotExchange`.

## Расширение для страницы отладки (Ф4, `head_web/`)

Поля ниже **не входят** в v1 ядра контура; страница их читает, если они
есть, и показывает «нет данных», если нет. Заполнять их будет Ф3/A-1.

| Поле | Содержимое |
|---|---|
| `bus.mode` | `bring-up-master` или `monitor` (в `monitor` голова только слушает, ручной ввод заблокирован) |
| `bus.frames_seen` | были ли кадры на шине (false → индикатор «нет кадров») |
| `bus.nodes[]` | `id`, `nmt` (`operational`, `pre-operational`, `stopped`, …), `hb_age_ms`, `hb_fresh` |
| `info.fw`, `info.config` | версии прошивки и конфига |

Транспортная ошибка WS (не ответ на команду):
`{"v":1,"type":"error","error":"..."}`; страница показывает её оператору,
команда при этом не считается принятой.
