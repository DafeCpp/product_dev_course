# CANopen-интеграция bench_controller

## third_party/CANopenNode — провенанс

- Upstream: https://github.com/CANopenNode/CANopenNode
- Версия: master (v4), коммит `bc79a5c87db84531b21a8b2d9611471a86220c70`
  («docs: clarify blank CAN pointer placeholder»), склонирован 2026-07-10
- Скопировано: `301/ 303/ 304/ 305/ 309/ storage/ extra/ CANopen.[ch]
  LICENSE` (без `doc/`, `example/`, `Doxyfile`)
- Локальные модификации: **нет** (код апстрима не правится; вся
  адаптация — снаружи: `od/`, `co_driver_hosted`, `co_master`)
- Лицензия: Apache-2.0 (см. `third_party/CANopenNode/LICENSE`)
- Путь миграции для продакшена: git submodule либо пакет реестра
  компонентов ESP-IDF — решение по итогам спайка (см. отчёт LOS-76)

## Состав интеграции

| Файл | Назначение |
|---|---|
| `od/OD.h/.c` | Объектный словарь мастера: example-OD апстрима + прикладные записи 0x2110/0x2111 (setpoint, control word) и 0x2120–0x2122 (force, position, status), правится руками |
| `co_driver_target.h` | Таргет-хедер CANopenNode для hosted-сборки |
| `i_can_bus.hpp` | `ICanBus` — фреймовый интерфейс шины (Send/Poll) |
| `co_driver_hosted.c` | Единый CO_driver поверх ICanBus (через C-шим) |
| `socketcan_bus.*` | ICanBus → Linux SocketCAN (vcan0 / can0) |
| `co_master.*` | C++-обёртка стека: init, NMT/HB, SDO-клиент, Process* |
| `canopen_valve_channel.*` | `IValveChannel` поверх OD (шов к ядру) |

## PDO-карта (мастер node 0x01, клапан node 0x20)

| PDO | COB-ID | Данные | Тип передачи |
|---|---|---|---|
| TPDO1 мастера | 0x220 (=RPDO1 клапана) | int16 setpoint, uint8 control word | 254 (event, каждый тик) |
| RPDO1 мастера | 0x1A0 (=TPDO1 клапана) | int16 force, int16 position, uint8 status | 254/synchronous — по сценарию |

SYNC-продюсер: COB 0x80, период 0x1006 = 2000 мкс (тик контура).
Heartbeat мастера 100 мс; consumer клапана — таймаут 200 мс →
`IValveChannel::IsOperational`.
