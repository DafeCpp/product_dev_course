# FW-S2.2 — Python replay (open-loop, batch) + инварианты

**Источник:** декомпозиция эпика [FW-S2](FW-S2-sil-physics-model-epic.md), 2026-06-17
**Тип:** инфраструктура тестирования (Фаза 1 — replay)
**Язык:** Python (драйвит `sim_host` из [FW-S2.1](FW-S2.1-sim-host-stdio-platform.md))
**Приоритет:** MEDIUM
**Статус:** [ ] Не начато
**Файлы (при реализации):** `firmware/sim/` (Python-пакет: загрузчик, драйвер,
pytest), `tests/fixtures/rides/` (golden-логи + провенанс).

## Зачем

Первый достоверностный уровень эпика: кормить пайплайн прошивки **записанными**
сенсорами и проверять, что выходы/инварианты не разъехались. Дёшево, детерминированно,
ловит регрессии в фильтрах/EKF/командном тракте. Использует `batch`-режим `sim_host`
(весь трейс разом — без пер-тикового IPC).

## Объём

- **Загрузчик** `telemetry_log_*.csv` → массив кадров (`TelemetryLogFrame`-схема:
  `ts_ms`, `rc_throttle/rc_steering`, `ax..gz`, `mx/my/mz`, EKF/heading, `test_marker`).
- **Драйвер batch-режима** `sim_host`: отправить трейс сенсоров+RC, получить трейс
  выходов прошивки.
- **Структура Python-проекта:** `firmware/sim/` (venv, `pyproject`/requirements,
  pytest, numpy).
- **pytest-инварианты (любой лог):** нет NaN/inf; `throttle/steering ∈ [-1,1]`;
  EKF-дисперсии (`ekf_vx_var/vy_var/r_var`) не расходятся; loop не залипает (число
  выходных кадров = числу входных).

**Фиделити — «со средней точки»:** CSV хранит уже калиброванный IMU, поэтому при
replay калибровку прошивки ставим в identity (через `set-params`), данные текут через
Madgwick/EKF/control как есть. Полный raw end-to-end записанных поездок — follow-up в
[FW-S2.7](FW-S2.7-ci-fixtures.md), завязан на [FW-S1](FW-S1-telemetry-transport-format-research.md).

Важно: **не оверфитить на точные float** — проверять инварианты/допуски, иначе тесты
хрупкие к мелким правкам фильтров.

## Критерии приёмки

- [ ] Один golden-лог гоняется через `sim_host` в batch-режиме.
- [ ] Инвариант-ассерты (NaN/диапазон/дисперсии/длина) зелёные в pytest.
- [ ] Документирована структура `firmware/sim/` и запуск (`venv` + `pytest`).

## Связанные

- [FW-S2.1](FW-S2.1-sim-host-stdio-platform.md) — exe/протокол (зависимость).
- **Блокирует:** [FW-S2.3](FW-S2.3-scenario-asserts.md),
  [FW-S2.7](FW-S2.7-ci-fixtures.md).
- **FW-R18** — `test_marker`/`drive_mode` упрощают нарезку golden-эпизодов
  (таск-файл появится отдельным PR).
