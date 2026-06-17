# FW-S2.4 — Физ-модель машинки на Python

**Источник:** декомпозиция эпика [FW-S2](FW-S2-sil-physics-model-epic.md), 2026-06-17
**Тип:** инфраструктура тестирования (Фаза 2 — физ-модель)
**Язык:** Python (numpy)
**Приоритет:** MEDIUM
**Статус:** [ ] Не начато
**Файлы (при реализации):** `firmware/sim/model/` (модель, `SimParams`, синтез
сенсоров), `firmware/sim/tests/` (pytest модели).

## Зачем

Цифровой двойник динамики машины: по командам (throttle/steering) интегрирует
состояние и **синтезирует сырые сенсоры**. Нужен для closed-loop SIL
([FW-S2.5](FW-S2.5-closed-loop-sil.md)) и валидации
([FW-S2.6](FW-S2.6-model-validation.md)). Пишется на Python — удобно для подгонки
(scipy) и графиков; в контур прошивки попадает через `sim_host`
([FW-S2.1](FW-S2.1-sim-host-stdio-platform.md)).

## Объём

**Уровни модели (наращиваем по нужде):**
- **Кинематический велосипед** — старт: `(x, y, ψ, v)`, `ψ̇ = v·tan(δ)/L`. Без сноса,
  но даёт yaw rate и траекторию.
- **Динамический велосипед** — боковой увод (cornering stiffness `Caf/Car`), масса,
  момент инерции, ЦМ. Под занос ([FW-R2](FW-R2-oversteer-slip-rate-spike.md)) и задний
  ход (**FW-R22**).

**Подмодели актуаторов/сенсоров (привязка к прошивке):**
- **Мотор:** команда → тяга с постоянной времени + мёртвая зона/breakaway
  (перекликается с [FW-R5](FW-R5-motion-driver-min-throttle.md), LinearRamp).
- **Серво руля:** лимиты хода и скорость (slew) — сверить с реальным диапазоном
  ([FW-R12](FW-R12-steering-throttle-limited-range.md)).
- **Синтез IMU (сырьё):** `accel` = прод./бок. ускорение + проекция g по
  тангажу/крену стенда (ориентация IMU относительно корпуса —
  [FW-R14](FW-R14-madgwick-yaw-drift-at-tilt.md)); `gyro_z = ψ̇`; bias/шум опц. (чтобы
  калибровке прошивки было что снимать).
- **Синтез mag:** из курса `ψ` + наклон рамки (под [FW-R3](FW-R3-mag-frame-mix.md)).
- Выход — в формате кадра протокола `sim_host`.

**`SimParams`:** `mass`, `wheelbase L`, ЦМ, `Caf/Car`, моторная постоянная времени,
лимиты серво, ориентация IMU, bias/шум. Значения — datasheet/обмеры + подгонка в
[FW-S2.6](FW-S2.6-model-validation.md).

## Критерии приёмки

- [ ] Модель выдаёт сырьё (IMU/mag) в формате кадра протокола.
- [ ] pytest на здравые свойства: руль → знак yaw rate; газ → разгон с τ; наклон →
  проекция g в accel; пределы серво соблюдаются.
- [ ] `SimParams` параметризованы и документированы (единицы, источник значений).

## Связанные

- [FW-S2](FW-S2-sil-physics-model-epic.md) — эпик.
- **Блокирует:** [FW-S2.5](FW-S2.5-closed-loop-sil.md),
  [FW-S2.6](FW-S2.6-model-validation.md).
- [FW-R2](FW-R2-oversteer-slip-rate-spike.md) / [FW-R3](FW-R3-mag-frame-mix.md) /
  [FW-R5](FW-R5-motion-driver-min-throttle.md) /
  [FW-R12](FW-R12-steering-throttle-limited-range.md) /
  [FW-R14](FW-R14-madgwick-yaw-drift-at-tilt.md) /
  **FW-R22** — привязка подмоделей к поведению.
