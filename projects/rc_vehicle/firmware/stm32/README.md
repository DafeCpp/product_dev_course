# STM32 прошивка для RC Vehicle

Прошивка для STM32 (F1/F4/G4), по функционалу аналогичная RP2040:
- PWM управление ESC и серво (50 Hz)
- Чтение RC-in сигналов (50 Hz)
- IMU MPU-6050 по SPI (50 Hz)
- Failsafe (таймаут 250 мс)
- UART мост к ESP32 (команды и телеметрия)

Используется **STM32Cube LL** (Low-Layer drivers) и CMSIS вместо libopencm3.

## Выбор микроконтроллера

Сборка под конкретный MCU задаётся переменной `MCU`:

| MCU            | Плата / чип        | Пакет STM32Cube   |
|----------------|--------------------|-------------------|
| `STM32F103C8`  | Blue Pill          | STM32CubeF1       |
| `STM32F401CE`  | Nucleo-F401RE      | STM32CubeF4       |
| `STM32F411CE`  | Black Pill         | STM32CubeF4       |
| `STM32G431CB`  | STM32G431CBTx      | STM32CubeG4       |

Добавить новый MCU: создать `cmake/boards/ИМЯ.cmake` и описать пины в `main/board_pins.hpp`.

## Технологии

- **Язык**: C++23 (C++26 при поддержке тулчейна)
- **Драйверы**: STM32Cube LL (Low-Layer) + CMSIS
- **Сборка**: CMake + Make, тулчейн arm-none-eabi-gcc

## Структура

```
firmware/stm32/
├── main/              # Исходники приложения
├── cmake/
│   ├── boards/        # Конфиги плат (MCU → Cube path, startup, linker)
│   └── cube_ll_sources.cmake  # Список LL-драйверов по семейству
├── CMakeLists.txt
├── Makefile
└── README.md
```

## Сборка

### Требования

1. **STM32Cube пакеты** (по одному на семейство):

   ```bash
   git clone --recursive https://github.com/STMicroelectronics/STM32CubeF1.git
   git clone --recursive https://github.com/STMicroelectronics/STM32CubeF4.git
   git clone --recursive https://github.com/STMicroelectronics/STM32CubeG4.git
   ```

2. **Тулчейн** (arm-none-eabi-gcc):

   **Linux (apt):**
   ```bash
   sudo apt-get install cmake gcc-arm-none-eabi libnewlib-arm-none-eabi build-essential
   ```

   **macOS (Homebrew):**
   ```bash
   brew install cmake
   brew install --cask gcc-arm-embedded
   ```

3. **Переменные окружения** (задайте путь к пакету для нужного MCU):

   ```bash
   export STM32CUBE_F1_PATH=/path/to/STM32CubeF1   # для F103C8
   export STM32CUBE_F4_PATH=/path/to/STM32CubeF4   # для F401/F411
   export STM32CUBE_G4_PATH=/path/to/STM32CubeG4   # для G431
   ```

### Команды make

Из каталога прошивки:

```bash
cd firmware/stm32
export STM32CUBE_F1_PATH=/path/to/STM32CubeF1

# Сборка для MCU по умолчанию (STM32F103C8)
make

# Сборка для выбранного MCU
make MCU=STM32F411CE
export STM32CUBE_F4_PATH=/path/to/STM32CubeF4
make MCU=STM32F411CE

# Очистка (при смене MCU — make clean, затем make MCU=...)
make clean
```

Артефакты: `build/rc_vehicle_stm32.elf`, `build/rc_vehicle_stm32.bin`, `build/rc_vehicle_stm32.hex`.

### Параллельная сборка

- Linux: `make -j$(nproc)`
- macOS: `make -j$(sysctl -n hw.ncpu)`

## Прошивка

- **ST-Link:** `st-flash write build/rc_vehicle_stm32.bin 0x08000000`
- **OpenOCD / STM32CubeIDE / Cortex-Debug** — по желанию.
- Из корня firmware: `make flash-stm32` (после `make build-stm32`).

## Конфигурация

- **Пины и периферия:** `main/board_pins.hpp` (по семейству F1/F4/G4).
- **Тайминги, UART, протокол:** `main/config.hpp`.

## Протокол UART

Тот же, что у RP2040: префикс `AA 55`, версия, тип сообщения, длина, payload, CRC16. COMMAND (ESP32 → STM32), TELEM (STM32 → ESP32).

## Статус

- [x] Структура проекта, выбор MCU (Makefile + CMake)
- [x] Платформа (SysTick, время), failsafe, protocol
- [x] Миграция на STM32Cube LL (CMSIS + LL, без libopencm3)
- [x] Заглушки PWM, RC-in, UART (логика в main готова)
- [x] IMU (MPU-6050) по SPI на LL (SPI2, PB12 NCS, PB13/14/15)
- [ ] Реализация PWM/RC-in/UART на LL под выбранную плату
- [ ] Проверка на железе

## Примечания

- При смене MCU выполните `make clean`, затем `make MCU=...` с нужным `STM32CUBE_*_PATH`.
- Структура каталогов в Cube (GitHub): `Drivers/cmsis_device_f1`, `Drivers/stm32f1xx_hal_driver`; в полном пакете с st.com — `Drivers/CMSIS/Device/ST/STM32F1xx`, `Drivers/STM32F1xx_HAL_Driver`. CMake поддерживает оба варианта.
