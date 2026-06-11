# FW-RF5 — Один snapshot конфига на итерацию control loop

**Источник:** `firmware/CODE_REVIEW.md` → Review 2026-06-10, RF5
**Приоритет:** MEDIUM (рефакторинг / производительность)
**Статус:** [ ] Не начато
**Файлы:** `common/control_loop_processor.cpp:30, 90`, `common/stabilization_manager.cpp:160-206`, `common/diagnostics_reporter.cpp:19`

## Проблема

За одну итерацию `Step()` (каждые 2 мс) `StabilizationConfig` копируется под
мьютексом минимум трижды:

1. `Step()` → `stab_mgr->GetConfig()` (`control_loop_processor.cpp:30`);
2. `UpdateWeights()` берёт собственную копию (`stabilization_manager.cpp:165-169`);
3. `PrintDiagnostics` → ещё `GetConfig()` (раз в интервал диагностики).

Это ~1000+ копий крупной структуры (~0.5 КБ) в секунду + контention
`config_mutex_` с WS-задачей (`SetConfig` при изменении настроек из UI).
На 500 Гц цикле — лишняя, легко устранимая нагрузка.

## Предлагаемое решение

1. В `Step()` снять **один** snapshot в начале итерации (сейчас он снимается
   в середине, строка 30 — перенести до `UpdateStabilization`; проверить,
   что `UpdateSensorsAndEkf` использует `GetConfig().filter.ekf_enabled` —
   тоже перевести на snapshot).
2. `UpdateWeights(uint32_t dt_ms)` → `UpdateWeights(const StabilizationConfig& cfg, uint32_t dt_ms)` —
   убрать внутреннюю копию.
3. Диагностика — использовать переданный snapshot (`stab_cfg_`).

Итог: ровно одна копия под локом на итерацию.

Опционально (если контention останется заметным): seqlock/двойной буфер с
`std::atomic<uint32_t>` версией вместо мьютекса — отдельным шагом, только
по результатам измерений `last_loop_hz`.

## Связанные задачи

- **FW-R1** — меняет `Step()`; делать ПОСЛЕ FW-R1, чтобы не конфликтовать.

## Объём работ

- [ ] Snapshot в начале `Step()`, прокинуть в `UpdateSensorsAndEkf`/`UpdateWeights`/диагностику
- [ ] Обновить сигнатуру `UpdateWeights` + вызовы в тестах
- [ ] Тесты `test_stabilization_manager.cpp`, `test_control_loop_processor.cpp` зелёные
- [ ] Проверить `last_loop_hz` на железе до/после (DIAG-лог) — не хуже

## Критерии приёмки

Одна копия конфига под мьютексом на итерацию; поведение стабилизации не
изменилось (тесты); loop Hz не деградировал.
