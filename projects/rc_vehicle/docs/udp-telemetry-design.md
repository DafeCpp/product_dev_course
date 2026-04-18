# UDP-стриминг телеметрии: дизайн-документ

**Статус:** Draft
**Автор:** —
**Дата:** 2026-03-20

---

## 1. Обзор и мотивация

Существующая система телеметрии RC Vehicle передает данные двумя способами:

1. **WebSocket JSON (20 Hz)** -- для веб-пульта в браузере. Формат удобен для отладки, но частота и overhead JSON непригодны для записи полных логов в реальном времени.
2. **Ring buffer (100 Hz)** -- запись `TelemetryLogFrame` в PSRAM. Доступ только постфактум через WebSocket-команды `get_log_data` с пагинацией по 200 кадров.

**Проблемы:**

- Нет потокового экспорта телеметрии в реальном времени на ПК.
- Выгрузка из ring buffer -- пакетная, медленная (JSON over WebSocket), теряет данные при переполнении буфера.
- JSON overhead: ~600 байт на кадр (vs 80 байт бинарных).
- WebSocket работает поверх TCP -- ретрансмиты добавляют латентность, непригодную для real-time визуализации.

**Решение:** UDP-стриминг бинарных `TelemetryLogFrame` с частотой 100 Hz. UDP идеален для телеметрии: потеря пакета допустима (следующий придет через 10 мс), а латентность минимальна.

**Пропускная способность:** 87 байт x 100 Hz = 8.7 КБ/с (~70 кбит/с) -- пренебрежимо мало для WiFi.

---

## 2. Формат пакета телеметрии

### 2.1 Бинарный фрейм (87 байт)

```
Offset  Size  Type        Field
──────  ────  ──────────  ──────────────────────────────
0       2     uint8[2]    Magic: 0x52, 0x54 ("RT")
2       1     uint8       Version: 0x01
3       4     uint32_t    Sequence number (LE, monotonic)
7       80    bytes       TelemetryLogFrame (memcpy, LE)
──────────────────────────────────────────────────────────
Total: 87 bytes
```

**Структура `TelemetryLogFrame` (80 байт, little-endian):**

| Offset (от начала frame) | Тип      | Поле              | Описание                       |
|--------------------------|----------|-------------------|--------------------------------|
| 0                        | uint32_t | ts_ms             | Timestamp [мс]                 |
| 4                        | float    | ax, ay, az        | Ускорение IMU [g]              |
| 16                       | float    | gx, gy, gz        | Угловая скорость IMU [dps]     |
| 28                       | float    | vx, vy            | EKF: скорость [м/с]           |
| 36                       | float    | slip_deg          | EKF: угол заноса [deg]        |
| 40                       | float    | speed_ms          | EKF: полная скорость [м/с]    |
| 44                       | float    | throttle          | Команда газа [-1..1]          |
| 48                       | float    | steering          | Команда руля [-1..1]          |
| 52                       | float    | pitch_deg         | Pitch [deg]                    |
| 56                       | float    | roll_deg          | Roll [deg]                     |
| 60                       | float    | yaw_deg           | Yaw [deg]                      |
| 64                       | float    | yaw_rate_dps      | Gyro Z filtered [dps]          |
| 68                       | float    | oversteer_active  | 1.0 = занос, 0.0 = нет        |
| 72                       | float    | rc_throttle       | Сырой газ с RC-приёмника [-1..1] |
| 76                       | float    | rc_steering       | Сырой руль с RC-приёмника [-1..1] |

**Обоснование формата:**

- Magic bytes позволяют отфильтровать мусорные пакеты.
- Sequence number детектирует потери (gap > 1 = lost packets).
- `memcpy` из `TelemetryLogFrame` -- zero-cost сериализация, не требует конверсии полей. ESP32-S3 и x86/ARM64 -- оба little-endian.
- Нет CRC: UDP уже имеет 16-bit checksum. Для WiFi в локальной сети этого достаточно.

---

## 3. UDP Control Protocol

ESP32 слушает фиксированный UDP порт **5556** для управляющих команд. Это позволяет управлять стримингом из Python-скриптов без WebSocket/браузера.

### 3.1 Формат команд (текстовый, UTF-8)

Команды -- однострочные текстовые сообщения. Ответы -- JSON (одна строка).

| Команда                        | Описание                           | Ответ                                                      |
|--------------------------------|------------------------------------|-------------------------------------------------------------|
| `START <port> [hz]`           | Начать стриминг на IP отправителя  | `{"ok":true,"ip":"<ip>","port":<port>,"hz":<hz>}`          |
| `STOP`                         | Остановить стриминг                | `{"ok":true}`                                               |
| `STATUS`                       | Запросить статус                   | `{"streaming":bool,"ip":"...","port":N,"hz":N,"seq":N,"dropped":N}` |
| `PING`                         | Проверка доступности               | `{"ok":true,"uptime_ms":N}`                                |

**Правила:**

- IP-адрес получателя телеметрии определяется автоматически из source IP пакета `START`. Это удобнее явного указания IP и безопаснее (нельзя случайно направить поток на чужой адрес).
- `hz` опционален, по умолчанию 100. Допустимые значения: 10, 20, 50, 100.
- Ответ отправляется на source IP:port команды.
- Неизвестные команды: `{"ok":false,"error":"unknown command"}`.
- Максимальная длина команды: 64 байта. Пакеты длиннее игнорируются.

### 3.2 Почему текстовый протокол

- Команд мало (4 штуки), частота отправки -- единицы в сессию.
- Легко тестировать из `netcat`, Python, любого языка.
- Overhead нерелевантен (десятки байт, разово).

---

## 4. WebSocket команды

Дублируют UDP control для управления из веб-пульта.

### 4.1 Запуск стриминга

```json
{
  "type": "udp_stream_start",
  "ip": "192.168.4.100",
  "port": 5555,
  "hz": 100
}
```

Ответ:

```json
{
  "type": "udp_stream_start_ack",
  "ok": true,
  "ip": "192.168.4.100",
  "port": 5555,
  "hz": 100
}
```

Ошибки: `ok: false` + поле `error` (invalid IP, invalid port, invalid hz, already streaming).

**Валидация:**
- `ip` -- обязательное, формат IPv4 (в отличие от UDP control, здесь IP явный, т.к. WebSocket-клиент может быть за NAT/proxy).
- `port` -- 1024..65535.
- `hz` -- 10, 20, 50, 100. По умолчанию 100.

### 4.2 Остановка

```json
{"type": "udp_stream_stop"}
```

Ответ:

```json
{"type": "udp_stream_stop_ack", "ok": true}
```

### 4.3 Статус

```json
{"type": "udp_stream_status"}
```

Ответ:

```json
{
  "type": "udp_stream_status",
  "streaming": true,
  "ip": "192.168.4.100",
  "port": 5555,
  "hz": 100,
  "seq": 12345,
  "dropped": 3
}
```

`dropped` -- количество кадров, потерянных из-за переполнения FreeRTOS очереди на стороне ESP32.

---

## 5. NVS Schema

Namespace: `udp_telem`

| Ключ        | Тип     | Описание                        | Значение по умолчанию |
|-------------|---------|---------------------------------|-----------------------|
| `target_ip` | string  | Последний target IP             | "" (пусто)            |
| `target_port` | u16   | Последний target port           | 5555                  |
| `hz`        | u8      | Последняя частота стриминга     | 100                   |

**Поведение:**

- При `START` (UDP или WebSocket) -- сохранить ip/port/hz в NVS.
- При старте ESP32 -- загрузить из NVS, но **не** начинать стриминг автоматически (требуется явная команда `START`).
- Python-клиент может отправить `START` без параметра port, если хочет использовать сохраненный (не реализуется в v1 -- всегда явный port).

---

## 6. C++ API модуля

Файл: `esp32_common/udp_telem_sender.hpp`

```cpp
#pragma once

#include <cstdint>
#include "telemetry_log.hpp"

/**
 * @brief Конфигурация UDP-стриминга телеметрии
 */
struct UdpTelemConfig {
    static constexpr uint16_t kControlPort = 5556;
    static constexpr uint16_t kDefaultDataPort = 5555;
    static constexpr size_t kQueueDepth = 64;
    static constexpr size_t kSenderTaskStack = 4096;
    static constexpr uint8_t kSenderTaskPriority = 3;
    static constexpr uint8_t kControlTaskPriority = 2;
    static constexpr size_t kMaxCommandLen = 64;
};

/**
 * @brief Инициализировать модуль UDP-телеметрии
 *
 * Создает:
 * - FreeRTOS очередь для TelemetryLogFrame (kQueueDepth элементов)
 * - UDP control socket на порту kControlPort
 * - Задачу udp_ctrl_task (прием команд START/STOP/STATUS/PING)
 * - Задачу udp_sender_task (отправка телеметрии из очереди)
 *
 * Загружает последний target из NVS (но не начинает стриминг).
 *
 * @return ESP_OK при успехе
 */
esp_err_t UdpTelemInit();

/**
 * @brief Поставить кадр телеметрии в очередь на отправку
 *
 * Вызывается из control loop (ISR-safe: нет, вызывать из задачи).
 * Если стриминг не активен -- no-op (проверка atomic bool, ~1 нс).
 * Если очередь полна -- кадр отбрасывается, счетчик dropped++.
 *
 * @param frame Кадр телеметрии
 */
void UdpTelemEnqueue(const TelemetryLogFrame& frame);

/**
 * @brief Запустить стриминг программно (без UDP/WS команды)
 *
 * @param ip IPv4 адрес получателя (строка "x.x.x.x")
 * @param port UDP порт получателя
 * @param hz Частота отправки (10, 20, 50, 100)
 * @return true при успехе
 */
bool UdpTelemStart(const char* ip, uint16_t port, uint8_t hz);

/**
 * @brief Остановить стриминг
 */
void UdpTelemStop();

/**
 * @brief Проверить, активен ли стриминг
 */
bool UdpTelemIsStreaming();

/**
 * @brief Получить текущий sequence number
 */
uint32_t UdpTelemGetSeq();

/**
 * @brief Получить количество отброшенных кадров
 */
uint32_t UdpTelemGetDropped();
```

**Обоснование C-style API (не класс):**

- Единственный экземпляр (singleton по сути).
- Вызывается из `ws_command_handlers.hpp` (C-linkage через cJSON), из control loop, из UDP control task -- три разных контекста.
- Паттерн идентичен существующему `websocket_server.hpp` (функции `WebSocketRegisterUri`, `WebSocketEnqueueTelem` и т.д.).

---

## 7. Интеграция в control loop

### 7.1 Точка вставки

В `vehicle_control_unified.cpp`, блок записи в ring buffer (строки 266-294). После `telem_mgr_->Push(frame)` добавить вызов `UdpTelemEnqueue(frame)`:

```cpp
// Запись в кольцевой буфер телеметрии — 100 Hz
if (imu_handler_ && imu_handler_->IsEnabled()) {
    const uint32_t last_log = telem_mgr_->GetLastLogTime();
    if (now - last_log >= config::TelemetryLogConfig::kLogIntervalMs) {
        TelemetryLogFrame frame;
        // ... заполнение frame (существующий код) ...
        telem_mgr_->Push(frame);
        telem_mgr_->SetLastLogTime(now);

        // >>> NEW: UDP telemetry streaming
        UdpTelemEnqueue(frame);  // no-op если стриминг не активен
    }
}
```

### 7.2 Частота ниже 100 Hz

Если `hz < 100`, sender task пропускает кадры. Реализация: при dequeue из очереди sender проверяет, прошло ли достаточно времени с последней отправки. Лишние кадры отбрасываются на стороне sender, а не в control loop -- control loop всегда enqueue с 100 Hz, что упрощает логику.

### 7.3 Влияние на control loop

- `UdpTelemEnqueue` при неактивном стриминге: проверка `std::atomic<bool>` -- ~1 нс, пренебрежимо.
- `UdpTelemEnqueue` при активном стриминге: `xQueueSend` с `timeout=0` (no-wait). Копирует 80 байт в очередь. Типичное время: <5 мкс. При бюджете итерации 2000 мкс (500 Hz) это 0.25%.
- Никаких аллокаций, мьютексов или блокирующих вызовов в control loop.

---

## 8. Внутренняя архитектура модуля

### 8.1 Задачи и сокеты

```
                     ┌──────────────────┐
                     │   Control Loop   │
                     │    (500 Hz)      │
                     └────────┬─────────┘
                              │ UdpTelemEnqueue() — каждые 10 мс
                              ▼
                     ┌──────────────────┐
                     │  FreeRTOS Queue  │
                     │   64 x 80 bytes  │  ← 5 КБ RAM
                     └────────┬─────────┘
                              │ xQueueReceive (portMAX_DELAY)
                              ▼
                     ┌──────────────────┐
                     │ udp_sender_task  │  prio=3, stack=4KB
                     │                  │
                     │ - rate limiting  │
                     │ - seq++          │
                     │ - build packet   │
                     │ - sendto()       │
                     └──────────────────┘
                              │
                              ▼
                        UDP socket
                     target_ip:data_port


    ┌──────────────────┐
    │ udp_ctrl_task    │  prio=2, stack=4KB
    │                  │
    │ - recvfrom(:5556)│
    │ - parse command  │
    │ - sendto reply   │
    │ - call Start/Stop│
    └──────────────────┘
```

### 8.2 Синхронизация

| Переменная          | Тип                   | Доступ                           |
|---------------------|-----------------------|----------------------------------|
| `s_streaming`       | `std::atomic<bool>`   | ctrl_task пишет, control loop читает |
| `s_target_ip`       | `uint32_t` (in_addr)  | ctrl_task пишет (только при !streaming) |
| `s_target_port`     | `uint16_t`            | ctrl_task пишет (только при !streaming) |
| `s_seq`             | `std::atomic<uint32_t>` | sender_task пишет, ctrl_task читает |
| `s_dropped`         | `std::atomic<uint32_t>` | control loop инкрементирует, ctrl_task читает |
| `s_hz`              | `uint8_t`             | ctrl_task пишет (только при !streaming) |
| `s_queue`           | `QueueHandle_t`       | control loop -> sender_task (FreeRTOS thread-safe) |

Мьютексы не требуются: target_ip/port/hz изменяются только при `!s_streaming`, а sender_task читает их только при `s_streaming`.

### 8.3 Приоритеты задач

| Задача            | Приоритет | Обоснование                                      |
|-------------------|-----------|--------------------------------------------------|
| control_loop      | 5         | Критическая -- управление моторами               |
| udp_sender_task   | 3         | Важнее control, но не критично при задержке      |
| udp_ctrl_task     | 2         | Обработка команд -- редкие события               |
| ws_telem_task     | 1         | Существующая WebSocket телеметрия                |

---

## 9. WebSocket handler

Новый файл не нужен. Добавляется в существующий `ws_command_handlers.hpp`:

```cpp
/**
 * @brief Start UDP telemetry streaming
 *
 * Request:  {"type":"udp_stream_start","ip":"192.168.4.100","port":5555,"hz":100}
 * Response: {"type":"udp_stream_start_ack","ok":true,"ip":"...","port":N,"hz":N}
 */
void HandleUdpStreamStart(cJSON* json, httpd_req_t* req);

/**
 * @brief Stop UDP telemetry streaming
 *
 * Request:  {"type":"udp_stream_stop"}
 * Response: {"type":"udp_stream_stop_ack","ok":true}
 */
void HandleUdpStreamStop(cJSON* json, httpd_req_t* req);

/**
 * @brief Get UDP telemetry streaming status
 *
 * Request:  {"type":"udp_stream_status"}
 * Response: {"type":"udp_stream_status","streaming":bool,...}
 */
void HandleUdpStreamStatus(cJSON* json, httpd_req_t* req);
```

Хендлеры вызывают `UdpTelemStart`/`UdpTelemStop`/`UdpTelemIsStreaming` из `udp_telem_sender.hpp`.

---

## 10. Python receiver/controller

Файл: `tools/udp_telem_receiver.py`

### 10.1 Режимы работы

```bash
# Приём телеметрии + запись в CSV
python udp_telem_receiver.py listen --port 5555 --csv output.csv

# Отправка команды start (ESP32 IP = 192.168.4.1)
python udp_telem_receiver.py start --esp 192.168.4.1 --port 5555 --hz 100

# Отправка команды stop
python udp_telem_receiver.py stop --esp 192.168.4.1

# Запрос статуса
python udp_telem_receiver.py status --esp 192.168.4.1

# Полный цикл: start + listen + stop по Ctrl+C
python udp_telem_receiver.py record --esp 192.168.4.1 --port 5555 --csv output.csv
```

### 10.2 Функциональность

- **Декодирование пакетов:** `struct.unpack('<2sBIIffffffffffffffffff', data)` -- валидация magic, version.
- **Детекция потерь:** сравнение seq с ожидаемым. Вывод в stderr: `[LOSS] expected seq=1234, got 1240 (lost 6 packets)`.
- **CSV-запись:** заголовок из имен полей `TelemetryLogFrame`. Flush каждые N строк (настраиваемо).
- **Статистика при завершении:** total packets, lost packets, loss %, duration, avg rate.
- **Graceful shutdown:** по Ctrl+C отправляет `STOP` на ESP32, закрывает CSV.

### 10.3 Зависимости

Только стандартная библиотека Python (socket, struct, csv, argparse, signal). Без внешних зависимостей.

---

## 11. Конфигурация

Добавить в `config.hpp`:

```cpp
namespace rc_vehicle::config {

struct UdpTelemConfig {
    static constexpr uint16_t kControlPort = 5556;
    static constexpr uint16_t kDefaultDataPort = 5555;
    static constexpr size_t kQueueDepth = 64;
    static constexpr size_t kSenderTaskStack = 4096;
    static constexpr uint8_t kSenderTaskPriority = 3;
    static constexpr uint8_t kControlTaskPriority = 2;
    static constexpr uint8_t kDefaultHz = 100;
};

}  // namespace rc_vehicle::config
```

---

## 12. Ограничения и edge cases

### 12.1 WiFi отвал (STA mode)

Если ESP32 в режиме STA и WiFi соединение потеряно:

- `sendto()` вернет ошибку. Sender task логирует warning (rate-limited, не чаще 1 раз/с), продолжает drain очередь.
- Стриминг **не останавливается** автоматически. При восстановлении WiFi -- пакеты пойдут снова.
- Обоснование: в AP-режиме (основной сценарий) WiFi не отваливается. В STA-режиме пользователь может переподключиться.

### 12.2 Переполнение очереди

- `xQueueSend` с timeout=0. Если очередь полна -- кадр отброшен, `s_dropped++`.
- 64 кадра x 10 мс = 640 мс буфер. Переполнение возможно если sender_task заблокирован (например, WiFi stack перегружен).
- Если dropped > 0, это видно через `STATUS` команду и в Python-логе.

### 12.3 Множественные клиенты

Только unicast, один получатель. Новый `START` с другим IP:port заменяет предыдущий (автоматический `STOP` + `START`). Обоснование: multicast усложняет код, а сценарий "два Python-скрипта слушают одновременно" нереалистичен для RC-машинки.

### 12.4 Фрагментация

79 байт < MTU (1500 байт для Ethernet, ~1460 для WiFi). Фрагментации нет.

### 12.5 Порядок инициализации

`UdpTelemInit()` вызывается **после** инициализации WiFi и до старта control loop. Порядок:

1. WiFi init (AP или STA)
2. HTTP server + WebSocket init
3. `UdpTelemInit()` -- создает сокеты, задачи, загружает NVS
4. Control loop start

### 12.6 Failsafe

При активации failsafe стриминг **продолжается** (телеметрия при failsafe тоже ценна для диагностики). Поля `throttle`/`steering` в кадре будут 0.0 (нейтраль).

### 12.7 IMU отключен

Если IMU выключен, кадры не записываются в ring buffer и не ставятся в UDP очередь (guard `if (imu_handler_ && imu_handler_->IsEnabled())` в control loop). Стриминг формально активен, но пакеты не отправляются.

### 12.8 Перезагрузка ESP32

При перезагрузке sequence number сбрасывается в 0. Python-клиент детектирует это как seq < prev_seq и выводит предупреждение (не считает потерей).

---

## 13. Потребление ресурсов

| Ресурс               | Потребление                   | Комментарий                      |
|-----------------------|-------------------------------|----------------------------------|
| RAM (очередь)        | 64 x 72 = 4608 байт          | Из основной RAM (не PSRAM)       |
| RAM (стеки задач)    | 2 x 4096 = 8192 байт         | sender + ctrl tasks              |
| RAM (сокеты)         | ~2 КБ                        | 2 UDP сокета (lwIP)              |
| RAM (итого)          | ~15 КБ                       |                                  |
| CPU (enqueue)        | <5 мкс на вызов              | 0.25% бюджета итерации           |
| CPU (sender)         | ~50 мкс на пакет             | sendto() + memcpy                |
| WiFi bandwidth       | ~63 кбит/с @ 100 Hz          | <1% пропускной способности       |
| NVS                  | 3 ключа, ~50 байт            |                                  |

---

## 14. Тестирование

### 14.1 Unit-тесты (GTest, без железа)

- Сериализация пакета: magic, version, sequence, payload size.
- Rate-limiting логика: при hz=50 каждый второй кадр пропускается.
- Парсинг UDP control команд: START, STOP, STATUS, PING, невалидные.

### 14.2 Интеграционные тесты (на железе)

- Запуск стриминга через UDP control, прием 1000 пакетов, проверка потерь < 1%.
- Запуск через WebSocket, прием на Python.
- Смена hz на лету (stop + start с другим hz).
- Переполнение очереди: искусственная задержка в sender, проверка dropped counter.
- NVS persistence: start, reboot, status -- ip/port/hz сохранены.

### 14.3 Ручные тесты

- `nc -u 192.168.4.1 5556` + ввод `PING` -- проверка control port.
- `python udp_telem_receiver.py record` -- полный цикл записи.

---

## 15. План реализации

| Шаг | Описание                                      | Файлы                                    |
|-----|-----------------------------------------------|------------------------------------------|
| 1   | Конфиг в config.hpp                           | `common/config.hpp`                      |
| 2   | Модуль udp_telem_sender                       | `esp32_common/udp_telem_sender.{hpp,cpp}` |
| 3   | WebSocket хендлеры                            | `esp32_s3/main/ws_command_handlers.{hpp,cpp}` |
| 4   | Интеграция в control loop                     | `common/vehicle_control_unified.cpp`     |
| 5   | Регистрация хендлеров в main                  | `esp32_s3/main/main.cpp`                |
| 6   | Python receiver/controller                    | `tools/udp_telem_receiver.py`            |
| 7   | Unit-тесты парсинга и сериализации            | `tests/test_udp_telem.cpp`               |
| 8   | Интеграционное тестирование на железе         | Ручное                                   |
