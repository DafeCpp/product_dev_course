# FW-RF4 — Kids-пресеты: единый источник истины

**Источник:** `firmware/CODE_REVIEW.md` → Review 2026-06-10, RF4
**Приоритет:** LOW (рефакторинг)
**Статус:** [x] Исправлено (PR, host-тесты + сборка esp32s3 зелёные)
**Файлы:** `esp32_s3/main/ws_command_handlers.cpp` (`HandleGetKidsPresets`), `common/stabilization_config.{hpp,cpp}` (`KidsModeConfig::ApplyPreset`, `GetKidsPresetTable`)

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

- [x] Таблица `KidsPresetInfo` + `GetKidsPresetTable()` в `stabilization_config`;
      `ApplyPreset()` берёт `throttle_limit`/`steering_limit` из таблицы (Custom — NaN)
- [x] `HandleGetKidsPresets` → цикл по `GetKidsPresetTable()`
- [x] Тесты `tests/unit/test_kids_mode.cpp`: `ApplyPresetMatchesPresetTable`
      (значения из `ApplyPreset` == таблица) и `PresetTableCoversAllPresets`

## Реализация

`stabilization_config.cpp` — таблица в анонимном namespace, единый источник:

```cpp
constexpr std::array<KidsPresetInfo, 4> kKidsPresetTable{{
    {KidsPreset::Custom,  "Custom",  "User-defined settings", NAN,   NAN},
    {KidsPreset::Toddler, "Toddler", "3-5 years old",         0.15f, 0.5f},
    {KidsPreset::Child,   "Child",   "6-9 years old",         0.30f, 0.7f},
    {KidsPreset::Preteen, "Preteen", "10-12 years old",       0.50f, 0.85f},
}};
```

`ApplyPreset()` читает `throttle_limit`/`steering_limit` из таблицы по `id`
(остальные поля пресета — газ-реверс, slew, anti-spin, accel/speed limit — в UI
не отображаются, остаются в `switch`). `HandleGetKidsPresets` итерирует таблицу;
для Custom (NaN) поля лимитов опускаются, как и прежде.

### Побочно: устранён рассинхрон UI↔поведение

Старый JSON отдавал `Toddler.throttle_limit = 0.2`, а `ApplyPreset` ставил
`0.15` — UI показывал не то, что применялось. Теперь оба берут `0.15` из таблицы.

## Проверка

- Host-тесты: 804 зелёных (новые `ApplyPresetMatchesPresetTable`,
  `PresetTableCoversAllPresets`; существующие `ApplyPreset*SetsCorrectValues`
  не изменились — значения те же).
- Сборка esp32s3 (ESP-IDF v6.0) зелёная; clang-format чистый.

## Критерии приёмки

Значения пресетов определены в одном месте; JSON-ответ совпадает с прежним
форматом (поля те же; Toddler.throttle_limit приведён к фактическому 0.15).
