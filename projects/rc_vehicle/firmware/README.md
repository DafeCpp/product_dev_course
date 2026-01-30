# Прошивки RC Vehicle

Общий код и платформенные прошивки для RC Vehicle (UART-мост ESP32 ↔ MCU, протокол, телеметрия, команды).

## Структура

| Каталог    | Описание |
|-----------|----------|
| `common/` | Общий код: протокол UART (`protocol.hpp/cpp`), базовый класс UART-моста (`uart_bridge_base.hpp/cpp`). Используется всеми платформами. |
| `esp32/`  | Прошивка ESP32 (ESP-IDF): Wi‑Fi AP, HTTP, WebSocket, UART-мост к MCU. |
| `rp2040/` | Прошивка Raspberry Pi Pico (Pico SDK): UART, PWM, RC-in, IMU, failsafe. |
| `stm32/`  | Прошивка STM32 (libopencm3): UART, PWM, RC-in, IMU, failsafe. |

Подробнее по каждому подпроекту — в `README.md` внутри соответствующего каталога.

## Сборка, заливка и монитор (Makefile)

В корне `firmware/` есть **Makefile** для сборки всех прошивок, заливки и мониторинга логов.

```bash
cd projects/rc_vehicle/firmware
make help
```

Основные цели:
- **Сборка:** `make build-all` или по отдельности: `make build-esp32`, `make build-rp2040`, `make build-stm32`
- **Очистка:** `make clean-all` или `make clean-esp32` / `clean-rp2040` / `clean-stm32`
- **Заливка:** `make flash-esp32`, `make flash-rp2040` (копирование .uf2 в RPI-RP2), `make flash-stm32` (st-flash)
- **Монитор логов:** `make monitor-esp32`, `make monitor-rp2040`, `make monitor-stm32`
- **Заливка + монитор:** `make flash-monitor-esp32`, `make flash-monitor-rp2040`, `make flash-monitor-stm32`

Переменные (при необходимости): `ESP32_PORT`, `RP2040_PORT`, `RP2040_UF2_VOL`, `STM32_PORT`, `STM32_MCU`. Подробнее: `make help`.

**Требования:** для сборки ESP32 нужен активированный ESP-IDF (`. export.sh`); для RP2040 — `PICO_SDK_PATH`; для STM32 — `LIBOPENCM3_PATH`. Для заливки STM32 — утилита `st-flash` (stlink).

## Сборка вручную (без Makefile)

- **ESP32:** нужен [ESP-IDF](https://docs.espressif.com/projects/esp-idf/). Из `esp32/`: `idf.py build`.
- **RP2040:** нужен [Pico SDK](https://github.com/raspberrypi/pico-sdk). Из `rp2040/`: `make` или `cmake -B build -S . && cmake --build build`.
- **STM32:** нужны пакеты [STM32Cube](https://github.com/STMicroelectronics/STM32CubeF1) (F1/F4/G4). Из `stm32/`: задать `STM32CUBE_F1_PATH` (или F4/G4), затем `make` или `make MCU=STM32F411CE`.

## Стандарты кода

Для всех прошивок (включая `common/`) действуют единые правила:

1. **C++23** — стандарт явно задан в CMake/IDF для каждой платформы.
2. **Форматирование** — [Google style](https://google.github.io/styleguide/cppguide.html) через clang-format. Конфиг: `firmware/.clang-format`.
   Пример (из корня `firmware/`):
   ```bash
   clang-format -i common/protocol.cpp main/uart_bridge.cpp
   ```
   Или для всех исходников:
   ```bash
   find . -name '*.cpp' -o -name '*.hpp' -o -name '*.h' | xargs clang-format -i
   ```
3. **Буферы** — по возможности без сырых указателей:
   - фиксированный размер → `std::array<uint8_t, N>`;
   - динамический размер → `std::vector<uint8_t>`;
   - API приёма/передачи буфера → `std::span<uint8_t>` / `std::span<const uint8_t>`.
   Виртуальные методы драйверов `Write(const uint8_t* data, size_t len)` и `ReadAvailable(uint8_t* buf, size_t max_len)` оставлены с указателями для совместимости с C-API (ESP-IDF, Pico SDK, libopencm3).

Соблюдение этих правил упрощает поддержку и перенос кода между платформами.
