# FW-CI4 — CI-отчёт покрытия host-тестов прошивки

**Источник:** запрос 2026-06-15 (после FW-RF8)
**Приоритет:** LOW (CI / наблюдаемость качества тестов)
**Статус:** [ ] Не начато
**Файлы:** `.github/workflows/firmware-tests.yml` (или новый
`firmware-coverage.yml`), `projects/rc_vehicle/firmware/tests/CMakeLists.txt`
(цель `coverage` уже есть)

## Описание

Host-тесты прошивки (807 GTest) гоняются в CI
(`.github/workflows/firmware-tests.yml`), но **покрытие не измеряется** — не
видно, какие модули `common/` реально протестированы, и нет защиты от
деградации при добавлении кода без тестов.

Инфраструктура уже есть: `tests/CMakeLists.txt` содержит опцию
`ENABLE_COVERAGE` и цель `coverage` (lcov + genhtml, `--coverage -O0 -g`,
исключения `/usr/*`, `*/_deps/*`, `*/tests/*`). Осталось завести её в CI.

## Решение (предлагаемое)

1. Job `coverage` (отдельный или шаг в `firmware-tests`):
   - `apt-get install lcov` (нужны `lcov` + `genhtml`).
   - `cmake -B build -DENABLE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug`.
   - `cmake --build build -j"$(nproc)" --target coverage`.
   - Артефакт: `tests/build/lcov-report/` (HTML) через `upload-artifact`.
2. Сводка покрытия в лог job (`lcov --summary lcov.info`) — видно % в UI без
   скачивания артефакта.
3. (Опционально, follow-up) Порог: падать, если line coverage ниже N%
   (`lcov --fail-under-lines N`, требует свежего lcov ≥ 2.x) — порог задать
   по фактическому базовому покрытию, не блокировать с первого PR.

## Объём работ

- [ ] Job/шаг coverage в workflow с установкой lcov
- [ ] Сборка с `ENABLE_COVERAGE=ON` + прогон цели `coverage`
- [ ] Публикация HTML-отчёта артефактом + summary в лог
- [ ] (Опц.) Порог покрытия после замера базового уровня

## Критерии приёмки

- В CI на PR с изменениями `projects/rc_vehicle/firmware/**` считается покрытие
  host-тестов, HTML-отчёт доступен артефактом, % виден в логе job.
- Триггеры/paths-фильтр — по конвенциям существующих firmware-workflows.

## Связанные

- **FW-CI1** — host-тесты прошивки в CI (предшественник, этот job расширяет).
- **FW-CI3** — clang-format в CI (тот же стиль paths-фильтра/триггеров).
