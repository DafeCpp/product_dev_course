# FW-R7 — Молчаливое усечение hz/port в WS-команде udp_stream_start

**Источник:** `firmware/CODE_REVIEW.md` → Review 2026-06-10, R7
**Приоритет:** LOW
**Статус:** [ ] Не начато
**Файлы:** `esp32_s3/main/ws_command_handlers.cpp:703-740`

## Проблема

`HandleUdpStreamStart` кастует параметры до валидации:

```cpp
uint16_t port = ... (uint16_t)port_item->valueint ...;
uint8_t hz   = ... (uint8_t)hz_item->valueint ...;
```

`hz=266` усекается до `10` — и `is_valid_hz(10)` в `UdpTelemStart()`
**принимает** его: клиент просил 266 Гц, молча получил 10 Гц без ошибки.
Аналогично `port=65536+5555` → `5555`.

## Предлагаемое решение

Валидировать в `int` до каста:

```cpp
int hz_i = (hz_item && cJSON_IsNumber(hz_item)) ? hz_item->valueint : 100;
int port_i = (port_item && cJSON_IsNumber(port_item)) ? port_item->valueint : 5555;
if (hz_i < 0 || hz_i > 255 || port_i < 1024 || port_i > 65535) {
  // ответ ok=false, error="invalid parameters"
}
```

После диапазонной проверки каст безопасен; точную валидацию значений hz
по-прежнему делает `is_valid_hz()`.

При выполнении вместе с FW-RF1 — использовать общий хелпер `GetInt(json, key,
default)` + диапазонную проверку.

## Объём работ

- [ ] Диапазонная валидация `hz`/`port` в int до каста, ошибка в ack при выходе за диапазон
- [ ] Проверка: `udp_stream_start` с `hz=266` → `ok=false` (а не «успех» с hz=10)

## Критерии приёмки

Любое значение вне диапазона отклоняется с `ok=false`; усечения значений нет.
