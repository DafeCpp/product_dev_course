# FW-R7 — Молчаливое усечение hz/port в WS-команде udp_stream_start

**Источник:** `firmware/CODE_REVIEW.md` → Review 2026-06-10, R7
**Приоритет:** LOW
**Статус:** [x] Исправлено (PR, сборка esp32s3 зелёная)
**Файлы:** `esp32_s3/main/ws_command_handlers.cpp` (`HandleUdpStreamStart`)

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

- [x] Диапазонная валидация `hz`/`port` в int до каста, ошибка в ack при выходе
      за диапазон (через общий `JsonGetIntChecked` из FW-RF1: port [1024,65535],
      hz [0,255]; `ok=false` при выходе)
- [x] При `!params_ok` ack отвечает `ok=false` с `error` про диапазон **до**
      вызова `UdpTelemStart` (усечения значений больше нет)

## Реализация

`HandleUdpStreamStart` теперь:

```cpp
bool params_ok = true;
int port_i = JsonGetIntChecked(json, "port", 5555, 1024, 65535, &params_ok);
int hz_i   = JsonGetIntChecked(json, "hz",   100,  0,    255,   &params_ok);
...
if (!params_ok) { ok=false; error="port out of [1024,65535] or hz out of [0,255]"; return; }
```

`hz=266` теперь даёт `ok=false`, а не молчаливое `hz=10`. Точную валидацию
конкретных значений hz по-прежнему делает `is_valid_hz()` в `UdpTelemStart`.

## Проверка

- Сборка esp32s3 (ESP-IDF v6.0) зелёная; clang-format чистый.
- Host-тестов нет: `ws_command_handlers.cpp` и `ws_json_util.hpp` зависят от
  ESP-IDF (`esp_http_server.h`) и в host-сборку не входят (как и весь FW-RF1).
  Поведенческая проверка `hz=266 → ok=false` — на железной WS-сессии.

## Критерии приёмки

Любое значение вне диапазона отклоняется с `ok=false`; усечения значений нет.
