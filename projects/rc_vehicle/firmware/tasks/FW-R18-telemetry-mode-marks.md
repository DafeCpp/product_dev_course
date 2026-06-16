# FW-R18 — Пометки режима и его состояния в скачиваемой телеметрии

**Приоритет:** MEDIUM
**Зона:** `common/telemetry_log.hpp`, `common/telemetry_builder.*`,
`esp32_s3/main/ws_command_handlers.cpp`, `esp32_common/web/app.js`
**Статус:** [ ] открыта

## Зачем

При анализе скачанной телеметрии не было видно, **какой режим** был активен и
**была ли включена стабилизация** на каждом сэмпле. Это всплыло при разборе
FW-R17 (дрожание руля): чтобы связать поведение с режимом/стабилизацией,
приходилось гадать. В кадре лога был только `test_marker`.

## Что сделано

- `TelemetryLogFrame`: добавлены `drive_mode` (uint8, 0=Normal..4=DirectLaw) и
  `stab_enabled` (uint8, 1/0). Влезли в padding — **размер кадра остался 128 Б**
  (`static_assert`).
- `BuildLogFrame()` принимает `drive_mode` + `stab_enabled` и пишет их в кадр;
  control loop передаёт `stab_cfg_.mode` / `stab_cfg_.enabled`.
- Экспорт CSV — обе ветки:
  - бинарная (`/api/log.bin` → `downloadBinaryLog`): новые поля в
    `FIELD_OFFSETS` (off 125/126, u8), колонки добавляются автоматически;
  - JSON (`get_log_data` → `exportLogCsv`): поля в JSON-кадре
    (`ws_command_handlers`) и в заголовке/строке CSV.

## Дальше (по желанию)

- Полные параметры режима (PID/slew/kids-лимиты) — событием `ModeChanged` при
  смене режима/конфига (колонки `event_type/event_param/event_value*` уже есть в
  CSV). Сейчас на каждый сэмпл пишется только режим + вкл/выкл стабилизации.

## Критерии приёмки

- [x] `drive_mode` + `stab_enabled` в кадре, размер 128 Б (тест round-trip).
- [x] Обе ветки экспорта CSV содержат новые колонки.
- [ ] На железе: в скачанном логе видно режим и стабилизацию на каждом сэмпле.
