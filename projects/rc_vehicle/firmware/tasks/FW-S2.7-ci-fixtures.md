# FW-S2.7 — CI-job (build exe + pytest), фикстуры, raw-log follow-up

**Источник:** декомпозиция эпика [FW-S2](FW-S2-sil-physics-model-epic.md), 2026-06-17
**Тип:** инфраструктура тестирования / CI (cross-cutting)
**Язык:** CI/docs
**Приоритет:** LOW
**Статус:** [x] Готово
**Файлы:** `.github/workflows/firmware-tests.yml` (job `SIL (sim_host + pytest)`).

## Зачем

Сделать SIL-симуляцию частью автоматического прогона и закрыть открытые
инфраструктурные вопросы эпика (хранение фикстур, нарезка, честность replay).

## Объём

- **CI-job:** сборка `sim_host` (cmake) + запуск pytest (модель
  [FW-S2.4](FW-S2.4-python-vehicle-model.md), replay-инварианты
  [FW-S2.2](FW-S2.2-python-replay-harness.md), сценарии
  [FW-S2.3](FW-S2.3-scenario-asserts.md)/[FW-S2.5](FW-S2.5-closed-loop-sil.md)).
  Существующий «GTest (host)» job остаётся независимым.
- **Хранение фикстур:** маленькие golden-вырезки в `tests/fixtures/rides/` с
  провенансом vs дампы целиком; git LFS при росте объёма.
- **Нарезка эпизодов:** ручной отбор по `test_marker`/`event_type`
  (**FW-R18**) vs авто.
- **Raw-log fidelity follow-up:** перейти от replay «со средней точки» к честному
  end-to-end (включая калибровку + Madgwick) для записанных поездок. Требует raw-лога
  сырых сенсоров → завязано на формат лога
  [FW-S1](FW-S1-telemetry-transport-format-research.md). После FW-S1 replay реальных
  логов тоже пойдёт через полный тракт прошивки.
- Связь с покрытием — [FW-CI4](FW-CI4-coverage-ci.md).

## Критерии приёмки

- [x] CI-job `SIL (sim_host + pytest)` в `firmware-tests.yml`: setup-python 3.12 →
  сборка `sim_host` (cmake `--target sim_host`) → `pip install -e ".[dev]"` →
  `pytest` с `SIM_HOST_BIN`. Триггер — изменения в `firmware/**`. «GTest (host)»
  остаётся отдельным job. Авто-подхватывает тесты S2.5/S2.6 по мере мержа.
- [x] Документированы решения по хранению/нарезке фикстур (ниже).
- [x] Follow-up (raw-log, git LFS) явно поставлены.

## Решения (зафиксировано)

- **Хранение фикстур:** маленькие golden-вырезки в `firmware/sim/tests/fixtures/rides/`
  (текстовый CSV, единицы килобайт) коммитятся в git с `PROVENANCE.md`. **git LFS НЕ
  нужен** при текущем объёме — вводить только если суммарный размер фикстур вырастет
  до десятков МБ. **Сырые полные логи (`telemetry_log_*.csv`) в git не коммитятся** —
  прогон локально/через `SIM_HOST_BIN`/`SIM_VALIDATION_LOG`.
- **Нарезка эпизодов:** ручной отбор по `test_marker`/`event_type` (**FW-R18**) —
  авто-нарезка пока не нужна.
- **Реальные golden-вырезки:** добавляются в [FW-S2.3](FW-S2.3-scenario-asserts.md)
  (наклон/занос/failsafe/задний ход) — там они и нужны по смыслу; сейчас в CI гоняется
  синтетический golden (FW-S2.2).
- **Raw-log fidelity:** follow-up, завязан на формат лога
  [FW-S1](FW-S1-telemetry-transport-format-research.md) — до него replay реальных логов
  идёт «со средней точки» (identity-калибровка). После FW-S1 — полный end-to-end тракт.

## Связанные

- [FW-S2.2](FW-S2.2-python-replay-harness.md) — даёт что гонять в CI (зависимость).
- [FW-S1](FW-S1-telemetry-transport-format-research.md) — формат лога для raw-log
  follow-up.
- [FW-CI4](FW-CI4-coverage-ci.md) — покрытие host-тестов.
