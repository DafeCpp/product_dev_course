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

Ограничения рига:

- Эмулятор — Python-процесс на том же хосте, его латентность ответа
  входит в RTT и отражает возможности рига, а не электроники Atos;
  выводы в отчёте это учитывают.
- Таймер `--fail-at-s` эмулятора отсчитывается от момента получения
  NMT-команды Start (≈ старт цикла контроллера), а не от запуска
  Python-потока — иначе задержка bootup-паузы и старта subprocess'а
  sim_host сдвигала бы событие относительно CSV-времени контроллера.
  Даже так, привязка «CSV `now_ms`» ↔ «реальный момент срабатывания»
  подвержена джиттеру планирования ОС на нагруженном dev-хосте (три
  Python-потока + subprocess конкурируют за CPU/GIL): в сериях
  прогонов задержка между конфигурированным и наблюдаемым в CSV
  моментом варьировалась от near-zero до сотен мс, хотя сам вызов
  `trigger_failure()` относительно старта контроллера стабильно точен
  (±2 мс). Для отчёта поэтому мера времени детекции — не «CSV `now_ms`
  при срабатывании детектора», а «сколько тиков контроллера прошло
  между появлением пониженной силы в его собственном потоке feedback
  и латчем» (не зависит от межпроцессной синхронизации часов).
