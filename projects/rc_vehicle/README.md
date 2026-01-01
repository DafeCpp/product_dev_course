# rc_vehicle

Проект про **управление RC‑моделью** (ретрофит): STM32 делает real‑time управление ESC/серво и сбор IMU, ESP32 поднимает Wi‑Fi (web‑пульт) и общается со STM32 по UART.

Сейчас здесь **документационный скелет**: требования, протоколы, договорённости. Реализация прошивок будет добавлена позже.

## Выбранная база (шасси)
- **Himoto SCT‑16 4WD RTR 1:16 2.4G (HI4192|RED)** — [карточка на Яндекс Маркете](https://market.yandex.ru/card/radioupravlyayemyy-short-kors-trak-himoto-sct-16-4wd-rtr-masshtab-116-24g---hi4192red/4399880474?do-waremd5=9jRAzA7SdRMQfoiYw52cAw&cpc=d2PGwzy0QTs0eLnRlKp0k5YCu2JHodVzFJDvJtshUWzSyDz3E9FKzWqRWc9gIULXX9EJ9Ku-0mvQsCTCCBzwqE-a6FjyS4PfsjEQ8zw1WU-FqmGO5HghkO4aplP4qdbhCmxXetVCLQqycN6o03nHXP_tLSTfOP2glbQ73t_oCvPUAJbjP6PtzyvAx9EZdLsM_MZD6JCx9hF2cmhA0Yc88aMXOZ_Ei1IVvZjbvcsvgHDewTubfOP7WxmSPsCnzZvbiXsTE30oPZ2PX6ct5WAkjK3DLf9791pMdJqhKAAkwv7LcDElOfELc6S0wxkUen63wrtSxWiKAo38hVIlcprRM84HQu9ao5TKsC9LjxYBM0hP5J5NaEfI0IHukClWKjyg9bwUETyJZKEokhHrUvGdfPVfDCgSMMCBr0cFhxl4T4GRgdt4k6KP_xjqOGhNoV-7&ogV=-9)

## Telemetry agent
CLI/агент для отправки телеметрии в Experiment Service вынесен в отдельный проект:
- `projects/telemetry_cli`

## Область ответственности
- управление (RC‑пульт и Wi‑Fi), приоритет RC > Wi‑Fi
- failsafe при потере источника управления
- IMU чтение и выдача телеметрии в web‑пульт (и/или наружу в будущем)

## Документация
- `docs/ts.md` — краткое ТЗ и критерии приёмки (MVP).
- `docs/interfaces_protocols.md` — протоколы: WebSocket (ESP32↔браузер) и UART кадры (ESP32↔RP2040).
- `docs/wiring_diagram.md` — **схема подключения компонентов** (ESP32, RP2040, IMU, ESC, Servo, RC приёмник).
- `docs/wiring_diagram.drawio` — визуальная схема для редактирования в draw.io.
- `docs/ai_schematic_tools.md` — инструменты и AI для генерации схем.
- `docs/glossary_ru.md` — глоссарий терминов и сокращений (ESC, BEC, PWM, IMU и т.д.).
- `docs/cpp_coding_style.md` — стиль кода для прошивок (C++): Google Style Guide, расширения `.hpp`, правила форматирования.
- `docs/firmware_timing.md` — тайминги и частоты для прошивок.
- (вне проекта) `docs/telemetry-rc-stm32.md` — рекомендации по формату телеметрии для Experiment Service, если будем экспортировать данные в ingest.

## Структура проекта
- `firmware/rp2040/` — прошивка RP2040 (PWM, RC‑in, IMU, failsafe, UART протокол) — *в разработке*
- `firmware/esp32/` — прошивка ESP32‑S3 (AP + web UI + WS + UART мост) — *в разработке*
- `docs/` — спецификации и схемы
  - `firmware_timing.md` — тайминги и частоты для прошивок

## Сборка прошивок

Для удобной сборки обеих прошивок используется Makefile в корне проекта:

```bash
# Показать все доступные команды
make help

# Собрать обе прошивки
make all

# Собрать только ESP32
make esp32-build

# Собрать только RP2040
make rp2040-build

# Очистить все проекты
make clean
```

Подробные инструкции по сборке каждой прошивки:
- ESP32: см. `firmware/esp32/README.md`
- RP2040: см. `firmware/rp2040/README.md`