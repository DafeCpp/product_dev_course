# FW-RF1 — Дедупликация WS-хендлеров

**Источник:** `firmware/CODE_REVIEW.md` → Review 2026-06-10, RF1
**Приоритет:** MEDIUM (рефакторинг)
**Статус:** [x] Реализовано (PR)
**Файлы:** `esp32_s3/main/ws_command_handlers.cpp` (842 строки), `ws_command_registry.cpp`

## Проблема

Каждый из ~25 хендлеров повторяет один и тот же скелет:

```cpp
cJSON* reply = cJSON_CreateObject();
if (reply) {
  cJSON_AddStringToObject(reply, "type", "..._ack");
  // ... поля ...
  WsSendJsonReply(req, reply);
  cJSON_Delete(reply);
}
```

Плюс повторяющийся паттерн парсинга входных полей
(`GetObjectItem` + `IsNumber` + каст с дефолтом). Последствия: объём,
copy-paste-ошибки, риск утечки cJSON при раннем return (сейчас обходится
дисциплиной).

## Предлагаемое решение

1. **RAII-обёртка** над `cJSON*` (`struct JsonDoc { cJSON* p; ~JsonDoc(){ cJSON_Delete(p); } }`)
   — убирает ручные `cJSON_Delete` и утечки при ранних return.
2. **Хелпер ответа**:
   ```cpp
   void WsReply(httpd_req_t* req, const char* type,
                const std::function<void(cJSON*)>& fill);
   // или без std::function: шаблон по callable
   ```
   добавляет `type`, вызывает `fill(reply)`, отправляет, удаляет.
3. **Хелперы парсинга**: `JsonGetFloat(json, key, def)`, `JsonGetInt(...)`,
   `JsonGetString(...)`, `JsonGetIntChecked(json, key, def, min, max, &ok)`
   (последний закрывает FW-R7).

Ожидаемый результат: файл ~400-450 строк, каждый хендлер — только своя логика.

## Связанные задачи

- **FW-R7** (валидация hz/port) — делать на новых хелперах.
- **FW-RF4** (kids-пресеты) — тот же файл, делать в одной ветке.

## Объём работ

- [x] RAII-обёртка + `WsReply` + `JsonGet*`-хелперы (отдельный заголовок, напр. `ws_json_util.hpp`)
- [x] Перевести все хендлеры на хелперы (механически, по одному)
- [ ] Прогнать существующие тесты; ручная проверка 3-4 команд через Web UI
      (host-тесты зелёные, прошивка собирается; Web UI — на железной сессии)

## Критерии приёмки

Поведение всех команд не изменилось (ack-форматы байт-в-байт, кроме
исправлений FW-R7); нет ручных `cJSON_Delete` в хендлерах.
