# benchsim — измерительный риг спайка LOS-76

Реальный CANopen-контур на виртуальной шине: `sim_host --socketcan`
(CANopenNode-мастер, контур 500 Гц с wall-clock пейсингом) против
эмулятора узла клапана (`benchsim.valve_node`, сырые кадры python-can).

Эмулятор осознанно написан на сырых кадрах (не CANopen-библиотека):
таймстампы python-can дают честный RTT, а протокольное поведение узла
явное — это независимая «вторая реализация» напротив CANopenNode.

## Prerequisites

```bash
sudo ./setup_vcan.sh          # vcan0 (один раз на сессию, нужен root)
python3 -m venv .venv && . .venv/bin/activate
pip install -e .
make -C ../firmware sim-build # sim_host
```

## Запуск

```bash
# полный прогон сценариев (a)–(e) → markdown-фрагменты для отчёта
python -m benchsim.scenarios \
  --sim-host ../firmware/tests/build/sim_host --out report_fragment.md

# вручную: эмулятор + мастер
python -m benchsim.valve_node --channel vcan0 &
../firmware/tests/build/sim_host --socketcan vcan0 --duration-s 5 > rig.csv
# посмотреть шину: candump vcan0
```

## Что измеряется

| Метрика | Как |
|---|---|
| Слежение (RMS/max) 10/20/50 Гц | CSV sim_host, force-режим |
| Свежесть feedback (% тиков) | колонка fresh CSV |
| RTT setpoint→feedback | таймстампы пар 0x220→0x1A0 (BusMonitor) |
| Нагрузка шины | биты/время по всем кадрам |
| Джиттер тика 500 Гц | TickStats sim_host (stderr) |
| SYNC vs event-driven | сценарий с --sync у эмулятора |
| Разрушение / потеря связи | флаги --fail-at-s / --link-loss-at-ms |

Ограничение рига: эмулятор — Python-процесс на том же хосте, его
латентность ответа входит в RTT и отражает возможности рига, а не
электроники Atos; выводы в отчёте это учитывают.
