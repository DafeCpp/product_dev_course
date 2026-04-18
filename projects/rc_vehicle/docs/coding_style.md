# Стиль кода для прошивок (C++)

## Общие правила

### Расширения файлов
- **Заголовочные файлы**: `.hpp` (не `.h`)
- **Исходные файлы**: `.cpp`
- Исключения разрешены, но должны быть обоснованы

### Стандарт языка
- **C++23** (где поддерживается)
- Минимально: **C++20**

### Стиль кода
- **Google C++ Style Guide** — основной стандарт
- Исключения разрешены

## Основные правила Google C++ Style Guide

### Именование
- **Классы**: `PascalCase` (например, `HttpServer`, `UartBridge`)
- **Функции**: `PascalCase` (например, `WiFiApInit`, `UartBridgeSendCommand`)
- **Переменные**: `snake_case` (например, `server_handle`, `uart_queue`)
- **Константы C++** (`const`, `constexpr`): `kUpperPascalCase` (например, `kUartBaudRate`, `kMaxConnections`)
- **Макросы** (`#define`): `UPPER_SNAKE_CASE` (например, `ESP_ERROR_CHECK`, `UART_BAUD_RATE`)
- **Приватные члены класса**: `snake_case_` с подчёркиванием в конце (например, `server_handle_`)

### Форматирование
- **Отступы**: 2 пробела (не табы)
- **Длина строки**: максимум 80 символов (можно до 100, если улучшает читаемость)
- **Фигурные скобки**: открывающая на той же строке (K&R style)
- **Пробелы**: вокруг операторов, после запятых, перед открывающей фигурной скобкой

Пример:
```cpp
void FunctionName(int param1, float param2) {
  if (condition) {
    DoSomething();
  } else {
    DoOtherThing();
  }
}
```

### Заголовочные файлы
- Использовать `#pragma once` вместо include guards
- Порядок включений:
  1. Соответствующий `.hpp` файл
  2. Системные заголовки (C/C++ стандартная библиотека)
  3. Заголовки ESP-IDF / платформы
  4. Другие заголовки проекта

Пример:
```cpp
#include "wifi_ap.hpp"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_wifi.h"

#include "config.hpp"

// Использование
esp_err_t ret = WiFiApInit();
```

### Комментарии
- Использовать `//` для однострочных комментариев
- Использовать `/* */` для многострочных комментариев (редко)
- Комментарии на английском или русском (по контексту)
- Документация функций: краткое описание, параметры, возвращаемое значение

Пример:
```cpp
/**
 * Инициализация Wi-Fi Access Point
 * @return ESP_OK при успехе, иначе код ошибки
 */
esp_err_t WiFiApInit(void);
```

### Указатели и ссылки
- Пробелы: `Type* ptr` (не `Type *ptr`)
- Инициализация: всегда инициализировать указатели (`nullptr`)

### Константы
- Использовать `const` и `constexpr` вместо `#define` где возможно
- Константы C++ именуются `kUpperPascalCase` (например, `constexpr int kUartBaudRate = 115200;`)
- `#define` только для конфигурации, макросов, include guards (именуются `UPPER_SNAKE_CASE`)

### Управление памятью
- Избегать `new`/`delete`, использовать умные указатели (`std::unique_ptr`, `std::shared_ptr`)
- В embedded: быть осторожным с динамической памятью (ограниченные ресурсы)

### Обработка ошибок
- Использовать коды возврата (`esp_err_t` для ESP-IDF)
- Проверять возвращаемые значения
- Логировать ошибки через `ESP_LOGE`, `ESP_LOGW`

## Исключения и особенности для embedded

### Разрешённые исключения
1. **C-style функции** для ESP-IDF API (например, `esp_wifi_init`)
2. **Макросы** для конфигурации (например, `#define UART_BAUD_RATE`)
3. **Глобальные переменные** для статических состояний модулей (например, `static esp_netif_t *ap_netif`)
4. **C-style строки** для работы с ESP-IDF API

### Особенности для embedded
- Минимизация использования динамической памяти
- Использование статических буферов где возможно
- Осторожность с исключениями C++ (могут быть отключены в некоторых конфигурациях)

## Инструменты

### Форматирование
- **clang-format** с конфигурацией Google style
- Файл конфигурации: `.clang-format` в корне проекта прошивки

## Примеры

### Хорошо
```cpp
#include "wifi_ap.hpp"

#include "esp_log.h"
#include "esp_wifi.h"

static const char *TAG = "wifi_ap";
constexpr int kMaxRetries = 3;

esp_err_t WiFiApInit(void) {
  // Инициализация
  esp_err_t ret = esp_wifi_init(&cfg);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize Wi-Fi");
    return ret;
  }
  return ESP_OK;
}
```

### Плохо
```cpp
#include "wifi_ap.h"  // Неправильное расширение
#include <esp_wifi.h> // Неправильный порядок

#define TAG "wifi"    // Должно быть const char*

esp_err_t wifi_ap_init(void)  // snake_case вместо PascalCase
{                      // Фигурная скобка на новой строке
    esp_err_t ret = esp_wifi_init(&cfg);  // 4 пробела вместо 2
    return ret;  // Нет проверки ошибок
}
```

## Ссылки

- [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)
- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/)

