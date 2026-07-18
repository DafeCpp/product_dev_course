# Bench Controller — прошивка контроллера прочностного стенда

Spike LOS-76: MCU-контур управления гидравлическим каналом нагружения
(500 Гц) — путь ①, выбранный по итогам discovery RFC-0004
([rfc-0004](../../../docs/RFC/rfc-0004-experiment-control-architecture.md),
workspace [docs/experiment-control/](../../../docs/experiment-control/)).

Архитектура зеркалит `projects/rc_vehicle/firmware/`: платформенно-
независимое ядро `common/` + платформенные слои + host-тесты и SIL.

## Состав (по фазам спайка)

| Каталог | Что | Фаза |
|---|---|---|
| `common/` | Ядро: регулятор force/displacement с bumpless-переключением, программа нагружения, детектор разрушения, наблюдатель связи (grace → плавная разгрузка), гидравлическая модель | PR-A |
| `tests/` | GTest (unit + integration, без CAN) + `sim_host` — ядро как CLI | PR-A |
| `canopen/` | CANopenNode v4 + драйверы (SocketCAN/vcan, TWAI) + адаптер IValveChannel | PR-B |
| `sim/` | Python-риг: эмулятор CANopen-узла клапана (python-canopen) + сценарии измерений | PR-B |
| `esp32/` | Таргет ESP32-S3: TWAI loopback, он-девайс измерения джиттера/RTT | PR-C |

STM32-таргет — отдельная задача после выбора платы и прихода
CAN-трансиверов (ядро уже компилируется хостовым тулчейном без
платформенных заголовков — переносимость обеспечена сборкой тестов).

## Сборка и тесты (host)

```bash
make -C projects/bench_controller/firmware test      # GTest
make -C projects/bench_controller/firmware sim-build # sim_host
```

## SIL-демо без CAN

```bash
tests/build/sim_host --duration-s 3 --freq 10 \
  --amplitude 10000 --mean 20000 > run.csv
# сценарий разрушения образца на 2-й секунде:
tests/build/sim_host --fail-at-ms 2000 > failure.csv
# потеря связи (grace → плавная разгрузка → hold):
tests/build/sim_host --link-loss-at-ms 1000 > linkloss.csv
```

CSV: `now_ms,mode,link,program_target,effective_target,valve_cmd,force_n,position_mm,failure`.

## Ключевые инварианты ядра (закреплены тестами)

- Команда клапану никогда не меняется быстрее slew-лимита — в том
  числе при переключении режимов и авариях (bumpless).
- Разрушение образца в force-режиме → displacement-hold не более чем
  за 2 цикла процесса.
- Потеря связи: MCU не продолжает программу автономно — grace-период,
  плавная разгрузка к среднему, SafeHold; возобновление только явной
  командой.
