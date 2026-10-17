# rc_vehicle

Проект про **управление RC‑моделью** (ретрофит): одна плата **ESP32‑S3 N16R8** делает всё сама: real‑time управление ESC и серво, чтение IMU и магнитометра, стабилизацию, Wi‑Fi (web‑пульт) и телеметрию. Отдельного микроконтроллера и UART‑моста нет.

## Выбранная база (шасси)
- **Himoto SCT‑16 4WD RTR 1:16 2.4G (HI4192|RED)** — [карточка на Яндекс Маркете](https://market.yandex.ru/card/radioupravlyayemyy-short-kors-trak-himoto-sct-16-4wd-rtr-masshtab-116-24g---hi4192red/4399880474?do-waremd5=9jRAzA7SdRMQfoiYw52cAw&cpc=d2PGwzy0QTs0eLnRlKp0k5YCu2JHodVzFJDvJtshUWzSyDz3E9FKzWqRWc9gIULXX9EJ9Ku-0mvQsCTCCBzwqE-a6FjyS4PfsjEQ8zw1WU-FqmGO5HghkO4aplP4qdbhCmxXetVCLQqycN6o03nHXP_tLSTfOP2glbQ73t_oCvPUAJbjP6PtzyvAx9EZdLsM_MZD6JCx9hF2cmhA0Yc88aMXOZ_Ei1IVvZjbvcsvgHDewTubfOP7WxmSPsCnzZvbiXsTE30oPZ2PX6ct5WAkjK3DLf9791pMdJqhKAAkwv7LcDElOfELc6S0wxkUen63wrtSxWiKAo38hVIlcprRM84HQu9ao5TKsC9LjxYBM0hP5J5NaEfI0IHukClWKjyg9bwUETyJZKEokhHrUvGdfPVfDCgSMMCBr0cFhxl4T4GRgdt4k6KP_xjqOGhNoV-7&ogV=-9)

## Архитектура

Целевая платформа — **ESP32‑S3 N16R8** (двухъядерный Xtensa LX7 на 240 МГц, PSRAM). Ядра разделены по задачам:

| Ядро | Что выполняется |
|------|-----------------|
| Core 1 | Control loop на **500 Гц**: IMU, фильтры (Madgwick, EKF), стабилизация, failsafe, PWM |
| Core 0 | Wi‑Fi AP, HTTP, WebSocket, телеметрия (в том числе UDP‑стриминг) |

Периферия: IMU MPU‑6500 и магнитометр MMC5983MA по SPI, ESC и серво через MCPWM, сигналы RC‑приёмника на GPIO. Схема подключения — `docs/wiring_diagram.md`.

Источники управления: RC‑пульт и Wi‑Fi (web‑пульт), приоритет **RC > Wi‑Fi**. При потере источника машина встаёт в нейтраль не позже чем через 250 мс (failsafe). Команда из WebSocket применяется напрямую к PWM.

Режимы езды: Normal, Sport, Drift, Kids и DirectLaw (без стабилизации). Параметры стабилизации хранятся в NVS и меняются через WebSocket.

## Область ответственности
- управление (RC‑пульт и Wi‑Fi), приоритет RC > Wi‑Fi
- failsafe при потере источника управления
- стабилизация: yaw rate, контроль угла заноса в дрифте, ограничители Kids Mode
- IMU, магнитометр, оценка скорости (EKF) и телеметрия в web‑пульт и по UDP

## Telemetry agent
CLI/агент для отправки телеметрии в Experiment Service вынесен в отдельный проект:
- `projects/telemetry_cli`

## Структура проекта
- `firmware/esp32_s3/` — прошивка ESP32‑S3 (ESP‑IDF): точка входа, PWM, RC, IMU, стабилизация
- `firmware/common/` — платформенно‑независимый код (протокол, калибровка IMU, Madgwick, EKF, control loop, failsafe)
- `firmware/esp32_common/` — общий код для ESP32 (Wi‑Fi, HTTP, WebSocket, NVS, web UI)
- `firmware/tests/` — GTest‑тесты без железа и `sim_host` для SIL
- `firmware/sim/` — SIL: физическая модель машинки и сценарии (`pytest`), см. `firmware/sim/README.md`
- `tools/` — анализ логов заездов: `analyze_telemetry.py` (проверки покоя), `trial_metrics.py` (метрики испытаний), `udp_telem.py`
- `docs/` — спецификации и схемы
- `stm32_espc3_schemas/` — KiCad‑схемы и отчёты ERC/DRC (исторический каталог, имя осталось от прежней двухплатной схемы)

## Документация

Актуальные документы:
- `firmware/README.md` — сборка прошивки, стандарты кода, pre‑commit.
- `firmware/esp32_s3/README.md` — прошивка ESP32‑S3: конфигурация пинов, подключение, логика управления.
- `docs/wiring_diagram.md` — **схема подключения компонентов** (ESP32‑S3, IMU, ESC, серво, RC‑приёмник); `docs/wiring_diagram.drawio` — визуальная схема для draw.io.
- `docs/interfaces_protocols.md` — протоколы: WebSocket (браузер ↔ ESP32), SPI/I2C датчиков.
- `docs/stabilization/` — система стабилизации: обзор, физика, настройка, роудмап.
- `docs/udp-telemetry-design.md` — UDP‑стриминг телеметрии.
- `docs/firmware_timing.md` — тайминги и частоты.
- `docs/ekf_calibration_reset.md` — сброс калибровок EKF.
- `firmware/TESTING_PROGRAM.md` — программа испытаний; `docs/indoor_test_checklist.md` — чек‑лист проверки на машине и разбор логов.
- `docs/bom_ru.md` — что купить; `docs/glossary_ru.md` — глоссарий терминов (ESC, BEC, PWM, IMU и т.д.).
- `docs/cpp_coding_style.md` — стиль кода для прошивок (C++), включая `clang-format-20`, pre‑commit hook и его установку.

Исторические материалы (схема STM32/RP2040 + ESP32 по UART, оставлены для справки):
- `docs/stm32g431_pinout_ru.md` — распиновка STM32G431.
- Приложение «UART‑протокол ESP32 ↔ внешний MCU (архив)» в `docs/interfaces_protocols.md` и разделы про RP2040 в `docs/firmware_timing.md`.
- `firmware/common/uart_bridge_base.*` — база UART‑моста, используется только в тестах.

Прочее: `docs/ai_schematic_tools.md` — инструменты и AI для генерации схем; `docs/schematic_review_ru.md`, `docs/schematic_check_report.md`, `docs/drc_fix_guide_ru.md`, `docs/kicad_pcb_settings_ru.md` — ревью и правки схемы.

## Сборка прошивки

Нужен активированный ESP‑IDF v6.1 (версия задаётся в `firmware/esp32_s3/.idf_version`), стандарт C++26. Сборка выполняется из корня проекта или из `firmware/`:

```bash
# Показать команды
make help

# Собрать прошивку (требуется активированный ESP-IDF)
make build

# Очистить сборку
make clean

# Залить и открыть монитор
make flash-monitor
```

## Тесты без железа

```bash
# GTest (host)
cd firmware && make test

# SIL: sim_host + pytest
cd firmware/tests && cmake -B build && cmake --build build
cd ../sim && pip install -e ".[dev]" && python -m pytest -q
```

Подробнее: `firmware/README.md`, `firmware/esp32_s3/README.md`, `firmware/sim/README.md`.
