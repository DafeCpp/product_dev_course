# FW-RF7 — slew_rate.hpp: namespace и устаревший комментарий

**Источник:** `firmware/CODE_REVIEW.md` → Review 2026-06-10, RF7
**Приоритет:** LOW (косметика)
**Статус:** [x] Выполнено (2026-06-11)
**Файлы:** `common/slew_rate.hpp`

## Проблема

1. `ApplySlewRate()` объявлена в глобальном namespace — единственная такая
   free function в `common/` (всё остальное в `rc_vehicle`).
2. Комментарий «Используется в main loop RP2040/STM32» устарел — платформа
   ESP32-S3.

## Предлагаемое решение

Обернуть в `namespace rc_vehicle`, поправить комментарий. Вызовы внутри
`namespace rc_vehicle` (kids_mode_processor, stabilization_manager,
control_loop_helpers) не изменятся; вызовы извне (если есть) — добавить
квалификацию.

## Объём работ

- [ ] namespace + комментарий
- [ ] `grep -rn ApplySlewRate` — проверить все call sites, сборка тестов

## Критерии приёмки

Сборка и тесты зелёные; функция в `rc_vehicle`.
