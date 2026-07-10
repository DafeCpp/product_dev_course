| Сценарий | RMS, Н | max, Н | fresh, % | RTT p99, мкс | нагрузка шины, % |
|---|---|---|---|---|---|
| синус 10 Гц (event) | 865 | 2978 | 96.3 | 1909 | 8.8 |
  <!-- tick: tick_us: min=11 avg=2001 p99=4000 max=9756 n=1999; fresh_feedback=1896/2000 (94.8%); valve_operational=1 -->
| синус 20 Гц (event) | 2286 | 13794 | 91.7 | 2128 | 8.7 |
  <!-- tick: tick_us: min=11 avg=1999 p99=4000 max=8558 n=1999; fresh_feedback=1819/2000 (91.0%); valve_operational=1 -->
| синус 50 Гц (event) | 2022 | 15295 | 90.5 | 2052 | 8.6 |
  <!-- tick: tick_us: min=11 avg=1999 p99=4000 max=10673 n=1999; fresh_feedback=1823/2000 (91.2%); valve_operational=1 -->
| синус 10 Гц (SYNC) | 1895 | 6817 | 94.3 | 2120 | 8.7 |
  <!-- tick: tick_us: min=11 avg=2000 p99=3940 max=7093 n=1999; fresh_feedback=1906/2000 (95.3%); valve_operational=1 -->

Разрушение на 2000 мс: латч на 1718 мс, итоговый режим disp/running
Потеря связи на 1500 мс: состояния ['grace', 'hold', 'ramp', 'running'], финал hold
