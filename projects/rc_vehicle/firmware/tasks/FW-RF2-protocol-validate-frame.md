# FW-RF2 — Общая валидация кадров протокола

**Источник:** `firmware/CODE_REVIEW.md` → Review 2026-06-10, RF2
**Приоритет:** LOW (рефакторинг)
**Статус:** [ ] Не начато
**Файлы:** `common/protocol.cpp:225-422`

## Проблема

Пять функций (`ParseTelemetry`, `ParseCommand`, `ParseLog`, `ParsePing`,
`ParsePong`) дословно повторяют блок ~30 строк: `ValidateHeader` → проверка
типа → `GetPayloadLength` → проверка длины → проверка размера буфера →
`ValidateCrc`. Различается только ожидаемый тип и правило для длины.

## Предлагаемое решение

```cpp
// Возвращает payload_len при успехе
static Result<uint16_t> ValidateFrame(std::span<const uint8_t> buffer,
                                      MessageType expected_type,
                                      std::optional<uint16_t> expected_len);
```

- `expected_len` задан → длина должна совпасть (`Telemetry`, `Command`,
  `Ping`/`Pong` с 0);
- `expected_len == nullopt` → проверка верхней границы делается вызывающим
  (`ParseLog` с `LOG_MAX_PAYLOAD`), либо добавить перегрузку с max_len.

Каждый `Parse*` сводится к `ValidateFrame(...)` + десериализация.

## Объём работ

- [ ] `ValidateFrame()` + перевод пяти `Parse*` на неё
- [ ] Существующие тесты `tests/unit/test_protocol.cpp` (819 строк) проходят без изменений — они и есть сетка безопасности
- [ ] Заодно (мелочь из FW-R8, если не сделана): `next_command_seq_` → `std::atomic`

## Критерии приёмки

Все тесты протокола зелёные без правок самих тестов; дублирование блока
валидации устранено.
