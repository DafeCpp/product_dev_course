# Прошивка RC Vehicle (ESP32-S3)

Единая прошивка для RC Vehicle на **ESP32-S3**: Wi‑Fi AP, HTTP, WebSocket, управление (PWM, RC-in, IMU, failsafe) и система стабилизации в одном чипе.

## Структура

| Каталог         | Описание |
|-----------------|----------|
| `common/`       | Общий код: протокол, UART-мост (база), SPI/IMU драйверы, калибровка IMU, Madgwick, control loop. |
| `esp32_common/` | Общий код для ESP32 (Wi‑Fi AP, HTTP, WebSocket, NVS калибровки). |
| `esp32_s3/`     | Прошивка ESP32-S3 (ESP-IDF): точка входа, HAL, PWM, RC, IMU, стабилизация. |

Подробнее — в `README.md` внутри `esp32_s3/`.

## Сборка, заливка и монитор (Makefile)

В корне `firmware/` есть **Makefile**:

```bash
cd projects/rc_vehicle/firmware
make help
```

Основные цели:
- **Сборка:** `make build`
- **Очистка:** `make clean`
- **Заливка:** `make flash` (опционально `ESP32_S3_PORT=/dev/cu.usbserial-XXX`)
- **Монитор логов:** `make monitor`
- **Заливка + монитор:** `make flash-monitor`

**Требования:** активированный ESP-IDF (`. export.sh` или `get_idf`).

## Сборка вручную (без Makefile)

Нужен [ESP-IDF](https://docs.espressif.com/projects/esp-idf/). Из каталога `esp32_s3/`:

```bash
idf.py build
idf.py flash
idf.py monitor
```

## Стандарты кода

- **C++23 (C++26 при поддержке тулчейна)** — стандарт задан в CMake/IDF.
- **Форматирование** — [Google style](https://google.github.io/styleguide/cppguide.html) через clang-format. Конфиг: `firmware/.clang-format`.
- **Буферы** — по возможности `std::array`, `std::vector`, `std::span`; минимизация сырых указателей.

Подробнее — в корневом README репозитория и `docs/cpp_coding_style.md`.
