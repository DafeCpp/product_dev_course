# FW-S2.7 — CI-job (build exe + pytest), фикстуры, raw-log follow-up

**Источник:** декомпозиция эпика [FW-S2](FW-S2-sil-physics-model-epic.md), 2026-06-17
**Тип:** инфраструктура тестирования / CI (cross-cutting)
**Язык:** CI/docs
**Приоритет:** LOW
**Статус:** [ ] Не начато
**Файлы (при реализации):** `.github/workflows/` (новый job), `tests/fixtures/rides/`
(политика хранения), возможно `.gitattributes` (git LFS).

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

- [ ] CI-job собирает `sim_host` и гоняет pytest; зелёный на PR.
- [ ] Документированы решения по хранению/нарезке фикстур.
- [ ] Follow-up (raw-log, git LFS) явно поставлены или закрыты как «не нужно».

## Связанные

- [FW-S2.2](FW-S2.2-python-replay-harness.md) — даёт что гонять в CI (зависимость).
- [FW-S1](FW-S1-telemetry-transport-format-research.md) — формат лога для raw-log
  follow-up.
- [FW-CI4](FW-CI4-coverage-ci.md) — покрытие host-тестов.
