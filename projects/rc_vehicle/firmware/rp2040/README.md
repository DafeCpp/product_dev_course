# RP2040 прошивка для RC Vehicle

Прошивка для Raspberry Pi Pico (RP2040), обеспечивающая:
- PWM управление ESC и серво (50 Hz)
- Чтение RC-in сигналов с приёмника (50 Hz)
- Работа с IMU (MPU-6050) через I2C (50 Hz)
- Failsafe защита от потери связи (таймаут 250 мс)
- UART мост к ESP32 для команд и телеметрии

## Технологии

- **Язык**: C++23
- **SDK**: Raspberry Pi Pico SDK
- **Целевая плата**: Raspberry Pi Pico (RP2040)

## Структура проекта

```
firmware/
├── common/                    # Общий код (RP2040 + STM32)
│   ├── protocol.hpp/cpp       # Протокол UART (AA 55, CRC16)
│   └── README.md
└── rp2040/
    ├── main/
    │   ├── main.cpp               # Точка входа
    │   ├── config.hpp             # Конфигурация (пины, частоты, тайминги)
    │   ├── pwm_control.cpp/hpp    # PWM управление ESC/серво
    │   ├── rc_input.cpp/hpp       # Чтение RC-in сигналов
    │   ├── imu.cpp/hpp            # Работа с IMU (MPU-6050)
    │   ├── failsafe.cpp/hpp       # Failsafe защита
    │   └── uart_bridge.cpp/hpp    # UART мост к ESP32 (использует common/protocol)
    ├── CMakeLists.txt             # Для Pico SDK
    ├── pico_sdk_import.cmake      # Импорт Pico SDK
    └── README.md
```

## Сборка

### Предварительные требования

1. **Установка Pico SDK**
   ```bash
   # Клонирование Pico SDK
   git clone https://github.com/raspberrypi/pico-sdk.git
   cd pico-sdk
   git submodule update --init
   ```

2. **Установка инструментов**

   **Linux (apt):**
   ```bash
   # CMake не ниже 3.13, ARM GCC, newlib, build-essential
   sudo apt-get install cmake gcc-arm-none-eabi libnewlib-arm-none-eabi build-essential
   ```

   **macOS (Homebrew):**
   ```bash
   brew install cmake
   brew install --cask gcc-arm-embedded
   ```
   Используйте **gcc-arm-embedded** (cask), а не `arm-none-eabi-gcc`: у последнего в Homebrew нет `nosys.specs`, сборка падает с ошибкой «cannot read spec file 'nosys.specs'». Если после установки `arm-none-eabi-gcc` не в PATH, добавьте в `~/.zshrc` или `~/.bash_profile`:
   ```bash
   export PATH="/Applications/ArmGNUToolchain/*/arm-none-eabi/bin:$PATH"
   ```
   (подставьте нужную версию вместо `*`, например `14.2.rel1`).

   **Если загрузка через Homebrew не удаётся** (ошибка `Could not resolve host: developer.arm.com` — сетевая/DNS проблема):
   1. Скачайте тулчейн вручную с [Arm GNU Toolchain Downloads](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) — раздел «Arm bare-metal target (arm-none-eabi)», пакет .pkg для macOS (arm64 или x64 по вашей системе). Скачать можно с другого компьютера/сети или при отключённом VPN.
   2. Установите .pkg, затем добавьте в PATH (подставьте свою версию):
      ```bash
      export PATH="/Applications/ArmGNUToolchain/15.2.Rel1/arm-none-eabi/bin:$PATH"
      ```

3. **Настройка окружения**
   ```bash
   export PICO_SDK_PATH=/path/to/pico-sdk
   ```

### Сборка проекта

Из корня прошивки (где лежит `Makefile`):

```bash
cd firmware/rp2040
export PICO_SDK_PATH=/path/to/pico-sdk   # если ещё не задан
make
```

Makefile сам создаёт `build/`, при необходимости запускает `cmake` и собирает проект. После сборки файл `rc_vehicle_rp2040.uf2` будет в `build/`.

**Команды make:**
- `make` — полная сборка (исполняемый файл и .uf2)
- `make clean` — удаление директории `build/` (полная пересборка при следующем `make`)
- Параллельная сборка: `make -j$(nproc)` (Linux) или `make -j$(sysctl -n hw.ncpu)` (macOS)

### Прошивка

1. **Через BOOTSEL режим**:
   - Зажмите кнопку BOOTSEL на Pico
   - Подключите Pico к компьютеру через USB
   - Отпустите кнопку BOOTSEL
   - Скопируйте `rc_vehicle_rp2040.uf2` на появившийся диск `RPI-RP2`

2. **Через picotool** (если установлен):
   ```bash
   picotool load rc_vehicle_rp2040.uf2
   picotool reboot
   ```

## Конфигурация

Основные параметры в `main/config.hpp`:

### Пины GPIO (настраиваются по схеме)
- `UART_TX_PIN` / `UART_RX_PIN`: UART для связи с ESP32
- `PWM_THROTTLE_PIN` / `PWM_STEERING_PIN`: PWM для ESC и серво
- `RC_IN_THROTTLE_PIN` / `RC_IN_STEERING_PIN`: Входы для RC сигналов
- `I2C_SDA_PIN` / `I2C_SCL_PIN`: I2C для IMU

### Частоты и тайминги
- `PWM_FREQUENCY_HZ`: 50 Hz (стандарт для RC)
- `PWM_UPDATE_INTERVAL_MS`: 20 мс (50 Hz)
- `RC_IN_POLL_INTERVAL_MS`: 20 мс (50 Hz)
- `IMU_READ_INTERVAL_MS`: 20 мс (50 Hz)
- `TELEM_SEND_INTERVAL_MS`: 50 мс (20 Hz)
- `FAILSAFE_TIMEOUT_MS`: 250 мс

### UART
- `UART_BAUD_RATE`: 115200 baud
- Протокол: бинарные кадры с префиксом `AA 55` и CRC16

## Протоколы

- **UART**: бинарные кадры с CRC16 (см. `docs/interfaces_protocols.md`)
  - COMMAND (ESP32 → RP2040): команды управления
  - TELEM (RP2040 → ESP32): телеметрия (IMU, статус)

## Логика работы

1. **Приоритет управления**: RC > Wi-Fi
   - Если RC сигнал активен, команды от Wi-Fi игнорируются
   - Если RC сигнал потерян, используются команды от Wi-Fi

2. **Failsafe**:
   - Если активный источник управления отсутствует > 250 мс:
     - Газ = 0 (нейтраль)
     - Руль = 0 (центр)

3. **Slew-rate limiting** (опционально):
   - Ограничение скорости изменения газа/руля для плавности

4. **Периодические задачи**:
   - PWM обновление: 50 Hz
   - RC-in опрос: 50 Hz
   - IMU чтение: 50 Hz
   - Телеметрия отправка: 20 Hz

## Отладка

Прошивка использует USB Serial для отладки (stdio). Подключитесь к `/dev/ttyACM0` (или аналогичному) для просмотра логов:

```bash
minicom -D /dev/ttyACM0 -b 115200
```

## Статус реализации

- [x] Базовая структура проекта
- [x] PWM управление
- [x] RC-in чтение
- [x] IMU поддержка (MPU-6050)
- [x] Failsafe
- [x] UART мост
- [x] Протокол UART (парсинг/формирование кадров)
- [ ] Тестирование на реальном железе
- [ ] Калибровка IMU
- [ ] Оптимизация таймингов

## Примечания

- Пины GPIO настраиваются в `config.hpp` в соответствии с вашей схемой
- Для работы с другими IMU (LSM6DSO, BMI270) потребуется адаптация кода в `imu.cpp`
- Slew-rate limiting можно отключить, установив большие значения в `config.hpp`

