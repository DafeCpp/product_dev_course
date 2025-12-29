# ESP32-S3 прошивка для RC Vehicle

Прошивка для ESP32-S3 Zero mini, обеспечивающая:
- Wi-Fi Access Point
- HTTP сервер для веб-интерфейса
- WebSocket сервер для команд управления и телеметрии
- UART мост к RP2040

## Технологии

- **Язык**: C++23
- **Фреймворк**: ESP-IDF v5.5 (требуется)
- **Целевая плата**: ESP32-S3 Zero mini

## Структура проекта

```
firmware/esp32/
├── main/
│   ├── main.cpp               # Точка входа
│   ├── wifi_ap.cpp/hpp        # Wi-Fi Access Point
│   ├── http_server.cpp/hpp    # HTTP сервер (раздача веб-интерфейса)
│   ├── websocket_server.cpp/hpp # WebSocket сервер
│   ├── uart_bridge.cpp/hpp    # UART мост к RP2040
│   ├── protocol.cpp/hpp       # Парсинг/формирование UART кадров
│   └── config.hpp             # Конфигурация (SSID, порты, частоты)
├── web/
│   ├── index.html             # Веб-интерфейс управления
│   ├── style.css              # Стили
│   └── app.js                 # JavaScript логика
├── CMakeLists.txt             # Для ESP-IDF
├── sdkconfig.defaults          # Настройки по умолчанию
└── README.md
```

## Сборка

### Предварительные требования

1. **Установка ESP-IDF v5.5**
   ```bash
   # Клонирование ESP-IDF
   git clone --recursive https://github.com/espressif/esp-idf.git
   cd esp-idf
   git checkout v5.5
   ./install.sh esp32s3
   ```

2. **Настройка окружения**
   ```bash
   # Активация окружения ESP-IDF (Linux/macOS)
   . ./export.sh

   # Или добавьте в ~/.bashrc / ~/.zshrc:
   alias get_idf='. $HOME/esp/esp-idf/export.sh'
   ```

3. **Проверка установки**
   ```bash
   idf.py --version
   # Должно показать версию 5.5.x
   ```

   **Важно**: Проект требует ESP-IDF версии 5.5. Другие версии могут не работать корректно.

### Сборка проекта

1. **Переход в директорию проекта**
   ```bash
   cd projects/rc_vehicle/firmware/esp32
   ```

2. **Настройка целевой платы (обязательно!)**
   ```bash
   idf.py set-target esp32s3
   ```

   **Важно**: Без установки целевой платы проект будет собираться для esp32 по умолчанию, что приведёт к ошибкам.

3. **Конфигурация (опционально)**
   ```bash
   idf.py menuconfig
   ```
   Основные настройки уже заданы в `main/config.hpp`, но можно изменить:
   - Частоту CPU
   - Размеры буферов
   - Настройки Wi-Fi

4. **Сборка прошивки**
   ```bash
   idf.py build
   ```

   При успешной сборке будет создан файл `build/rc_vehicle_esp32.bin`

### Прошивка на устройство

1. **Подключение устройства**
   - Подключите ESP32-S3 Zero mini к компьютеру через USB
   - Определите порт (Linux: `/dev/ttyUSB0` или `/dev/ttyACM0`, macOS: `/dev/cu.usbserial-*`, Windows: `COM*`)

2. **Прошивка**
   ```bash
   # Автоматическое определение порта
   idf.py flash

   # Или с указанием порта
   idf.py -p /dev/ttyUSB0 flash
   ```

3. **Мониторинг (просмотр логов)**
   ```bash
   # Запуск мониторинга после прошивки
   idf.py monitor

   # Или одной командой: прошивка + мониторинг
   idf.py flash monitor

   # Выход из мониторинга: Ctrl+]
   ```

### Быстрые команды

```bash
# Полная сборка и прошивка с мониторингом
idf.py build flash monitor

# Очистка проекта
idf.py fullclean

# Очистка и пересборка
idf.py fullclean build

# Просмотр размера прошивки
idf.py size
idf.py size-components
idf.py size-files
```

### Решение проблем

#### Проблема: "Permission denied" при прошивке (Linux)
```bash
# Добавить пользователя в группу dialout
sudo usermod -a -G dialout $USER
# Перелогиниться или выполнить:
newgrp dialout
```

#### Проблема: Порт не найден
```bash
# Проверить доступные порты
ls /dev/ttyUSB* /dev/ttyACM*  # Linux
ls /dev/cu.*                   # macOS
```

#### Проблема: Ошибки компиляции
- Убедитесь, что используете ESP-IDF v5.5 (проверка: `idf.py --version`)
- **Установите целевую плату**: `idf.py set-target esp32s3` (обязательно перед первой сборкой!)
- Проверьте, что все зависимости установлены: `idf.py install`
- Очистите проект: `idf.py fullclean`

#### Проблема: Недостаточно памяти
- Используйте `idf.py size` для анализа использования памяти
- Отключите неиспользуемые компоненты в `menuconfig`

### Альтернативные способы сборки

#### PlatformIO
```ini
; platformio.ini
[env:esp32-s3-devkitc-1]
platform = espressif32
board = esp32-s3-devkitc-1
framework = espidf
board_build.mcu = esp32s3
```

#### Arduino Framework
Использовать Arduino IDE с поддержкой ESP32-S3 (требует адаптации кода).

## Стиль кода

- Заголовочные файлы: `.hpp` (не `.h`)
- Стандарт: Google C++ Style Guide
- Подробности: см. `docs/cpp_coding_style.md`

## Конфигурация

Основные параметры в `main/config.hpp`:
- SSID Wi-Fi AP
- Пароль (опционально)
- Порт HTTP (80)
- Порт WebSocket (81)
- Скорость UART (115200)
- Частоты отправки команд/телеметрии

## Протоколы

- **WebSocket**: JSON команды и телеметрия (см. `docs/interfaces_protocols.md`)
- **UART**: бинарные кадры с CRC16 (см. `docs/interfaces_protocols.md`)

## Статус реализации

- [ ] Wi-Fi AP
- [ ] HTTP сервер
- [ ] WebSocket сервер
- [ ] UART мост
- [ ] Протокол UART (парсинг/формирование кадров)
- [ ] Веб-интерфейс

