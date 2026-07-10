| Сценарий | RMS, Н | max, Н | fresh, % | RTT p99, мкс | нагрузка шины, % |
|---|---|---|---|---|---|
| синус 10 Гц (event) | 2082 | 7979 | 89.1 | 2205 | 8.7 |
  <!-- tick: tick_us: min=10 avg=1998 p99=4000 max=10229 n=1999; fresh_feedback=1818/2000 (90.9%); valve_operational=1 -->
| синус 20 Гц (event) | 3431 | 11075 | 96.2 | 1732 | 8.7 |
  <!-- tick: tick_us: min=11 avg=2000 p99=4000 max=17108 n=1999; fresh_feedback=1899/2000 (95.0%); valve_operational=1 -->
| синус 50 Гц (event) | 2929 | 16549 | 94.3 | 1868 | 8.8 |
  <!-- tick: tick_us: min=11 avg=2000 p99=4000 max=8853 n=1999; fresh_feedback=1878/2000 (93.9%); valve_operational=1 -->
| синус 10 Гц (SYNC) | 1832 | 4564 | 97.9 | 2022 | 8.8 |
  <!-- tick: tick_us: min=11 avg=1999 p99=3070 max=7094 n=1999; fresh_feedback=1948/2000 (97.4%); valve_operational=1 -->

Разрушение на 2000 мс: латч на 2020 мс (CSV-время подвержено джиттеру рига — см. sim/README.md), итоговый режим disp/running
Потеря связи на 1500 мс: состояния ['grace', 'hold', 'ramp', 'running'], финал hold
