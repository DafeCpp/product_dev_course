# FW-R11 — Новый пароль STA не применяется на лету (нужна перезагрузка)

**Источник:** железная сессия 2026-06-13 (тест ветки test/wave-4-and-rf1)
**Приоритет:** MEDIUM (UX: «Подключить» из Web UI молча не срабатывает)
**Файлы:** `esp32_common/wifi_ap.cpp` (`WiFiStaConnect`)

## Проблема

При смене Wi-Fi-кредов через Web UI (POST `/api/wifi/sta/connect`) новый
пароль сохранялся в NVS, но **не применялся к работающему драйверу** — STA
продолжала ретраить со старым паролем, и подключение происходило только
после ручной перезагрузки.

Сценарий с железа: в NVS лежал устаревший пароль (`pass_len=9` вместо
реальных 11). После загрузки STA уходила в цикл ретраев и постоянно
находилась в состоянии «connecting». В этот момент:

```cpp
e = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);  // <-- "sta is connecting,
if (e != ESP_OK) return e;                       //     cannot set config"
(void)esp_wifi_disconnect();                     //     не выполняется
e = esp_wifi_connect();                          //     не выполняется
```

`esp_wifi_set_config` возвращает ошибку «sta is connecting, cannot set
config», функция выходит по `return e` — живой драйвер так и не получает
новый конфиг, а `disconnect()`/`connect()` ниже не вызываются. NVS при этом
уже перезаписан (`SaveStaCreds` выше по коду), поэтому после перезагрузки
конфиг грузится из NVS заново и подключение проходит. Отсюда симптом «после
перезагрузки подключилось» и спам `E wifi:sta is connecting, cannot set
config` от авто-ретраев Web UI.

## Решение

Выйти из состояния «connecting» **до** `esp_wifi_set_config`: сделать
`esp_wifi_disconnect()` перед сменой конфига. На время реконфигурации
сбросить `s_sta_should_connect`, чтобы обработчик `STA_DISCONNECTED` не
переподключился со старым конфигом и не вернул STA в «connecting»:

```cpp
portENTER_CRITICAL(&s_wifi_mux);
s_sta_should_connect = false;
portEXIT_CRITICAL(&s_wifi_mux);
(void)esp_wifi_disconnect();

e = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);

portENTER_CRITICAL(&s_wifi_mux);
s_sta_should_connect = true;
portEXIT_CRITICAL(&s_wifi_mux);

if (e != ESP_OK) return e;
return esp_wifi_connect();
```

NVS-сохранение (`SaveStaCreds`) остаётся выше и не меняется — оно и так
отрабатывало корректно.

## Объём работ

- [x] Реордер `disconnect` → `set_config` → `connect` в `WiFiStaConnect`
- [x] Глушение авто-reconnect (`s_sta_should_connect`) на окне реконфигурации

## Критерии приёмки

- [x] Железо (2026-06-14): смена пароля из Web UI применяется **без
      перезагрузки** — STA подключается к новой сети сразу после «Подключить»
- [x] Железо (2026-06-14): при вводе верного пароля поверх неверного из NVS
      подключение проходит с первой попытки, без спама «sta is connecting»
- [x] Прошивка собирается (esp32s3, ESP-IDF v6.0), host-тесты зелёные

## Примечания

Host-тестов на `esp32_common/wifi_ap.cpp` нет (код завязан на ESP-IDF
Wi-Fi API), проверка — компиляция + железная сессия.
