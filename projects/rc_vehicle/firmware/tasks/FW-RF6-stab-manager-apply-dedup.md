# FW-RF6 — StabilizationManager: дедупликация SetConfig/ApplyConfig

**Источник:** `firmware/CODE_REVIEW.md` → Review 2026-06-10, RF6
**Приоритет:** LOW (рефакторинг)
**Статус:** [x] Выполнено (2026-06-11)
**Файлы:** `common/stabilization_manager.cpp:79-92, 141-158`, `:39`

## Проблема

1. Блок «применить конфиг к фильтрам» (beta, adaptive beta, LPF cutoff,
   Madgwick enable) продублирован в `SetConfig()` (`:80-87`) и
   `ApplyConfig()` (`:148-157`); правки одного места легко забыть во втором.
2. В диагностическом логе magic захардкожен:
   `ESP_LOGE(..., "magic=0x%08X (expected 0x%08X)", ..., 0x53544232)` —
   дублирует константу из `stabilization_config.hpp` (рассинхрон при смене
   версии магика).

## Предлагаемое решение

1. Приватный метод `ApplyToFilters(const StabilizationConfig& cfg)` — единое
   место; `SetConfig` и `ApplyConfig` вызывают его.
2. В лог — именованную константу (`StabilizationConfig::kMagic` или что
   определено в конфиге).

## Объём работ

- [ ] `ApplyToFilters()` + замена двух блоков
- [ ] Константа магика в логе
- [ ] Тесты `test_stabilization_manager.cpp` зелёные

## Критерии приёмки

Применение конфига к фильтрам — один метод; магик в логе берётся из
константы конфига.
