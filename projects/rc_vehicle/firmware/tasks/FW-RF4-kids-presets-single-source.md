# FW-RF4 — Kids-пресеты: единый источник истины

**Источник:** `firmware/CODE_REVIEW.md` → Review 2026-06-10, RF4
**Приоритет:** LOW (рефакторинг)
**Статус:** [ ] Не начато
**Файлы:** `esp32_s3/main/ws_command_handlers.cpp:310-368`, `common/stabilization_config.{hpp,cpp}` (KidsConfig::ApplyPreset)

## Проблема

`HandleGetKidsPresets` вручную собирает JSON с захардкоженными значениями
(`throttle_limit: 0.2/0.3/0.5`, `steering_limit: 0.5/0.7/0.85`, имена,
описания). Те же числа живут в `KidsConfig::ApplyPreset()`. При изменении
пресета легко получить рассинхрон между фактическим поведением и тем, что
показывает UI.

## Предлагаемое решение

Таблица описателей рядом с `ApplyPreset` (в `stabilization_config`):

```cpp
struct KidsPresetInfo {
  KidsPreset id;
  const char* name;
  const char* description;
  float throttle_limit;   // NAN для Custom
  float steering_limit;
};
std::span<const KidsPresetInfo> GetKidsPresetTable();
```

`ApplyPreset()` берёт значения из таблицы; `HandleGetKidsPresets` итерирует
таблицу циклом. Числа существуют в одном месте.

## Связанные задачи

- **FW-RF1** — тот же файл; делать в одной ветке после внедрения хелперов.

## Объём работ

- [ ] Таблица + переключение `ApplyPreset()` на неё
- [ ] `HandleGetKidsPresets` → цикл по таблице
- [ ] Тест `tests/unit/test_kids_mode.cpp`: значения из `ApplyPreset` совпадают с таблицей

## Критерии приёмки

Значения пресетов определены в одном месте; JSON-ответ совпадает с прежним
форматом.
