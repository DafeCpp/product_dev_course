# Прошивка RC Vehicle (ESP32-S3)

Единая прошивка для RC Vehicle на **ESP32-S3**: Wi‑Fi AP, HTTP, WebSocket, управление (PWM, RC-in, IMU, failsafe) и система стабилизации в одном чипе.

## Структура

| Каталог         | Описание |
|-----------------|----------|
| `common/`       | Общий код: протокол, SPI/IMU драйверы, калибровка IMU, Madgwick, EKF, control loop, failsafe. Остался `uart_bridge_base.*` от прежней двухплатной схемы, он используется только в тестах. |
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
cd projects/rc_vehicle/firmware/esp32_s3
./scripts/migrate_cpu_freq_sdkconfig.sh
idf.py build
idf.py flash
idf.py monitor
```

## Стандарты кода

- **C++26** для прошивки ESP32-S3 — обязателен (веб-ресурсы вшиваются через `#embed`), нужен ESP-IDF v6.1 (GCC 15+); стандарт задан в `esp32_s3/main/CMakeLists.txt`. Код `common/` остаётся совместимым с C++23: host-тесты собираются на C++23.
- **Форматирование** — [Google style](https://google.github.io/styleguide/cppguide.html) через clang-format. Конфиг: `firmware/.clang-format`.
- **Буферы** — по возможности `std::array`, `std::vector`, `std::span`; минимизация сырых указателей.

## Pre-commit: clang-format

В корне репозитория есть pre-commit hook для C/C++-файлов из этого каталога.
Он запускает `clang-format-20`, применяет существующий `firmware/.clang-format`
и пропускает generated code, зависимости и каталоги сборки.

Установите зависимости и hook один раз:

```bash
sudo apt-get install clang-format-20
python3 -m pip install pre-commit
./scripts/install-pre-commit-hook.sh
```

Проверить или отформатировать все отслеживаемые файлы прошивки вручную:

```bash
pre-commit run rc-vehicle-clang-format --all-files
```

Подробнее — в корневом README репозитория и `docs/cpp_coding_style.md`.
