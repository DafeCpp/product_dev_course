# ESP32‑S3 прошивка для RC Vehicle (управление + веб)

Прошивка для **ESP32‑S3** (ESP‑IDF), которая совмещает в одном чипе:

- **Управление машиной**: PWM (ESC + servo), опционально RC‑in, IMU (MPU‑6050/MPU‑6500 по SPI), failsafe, slew‑rate.
- **Веб‑интерфейс**: Wi‑Fi Access Point, HTTP страница управления, WebSocket для команд/телеметрии.

В отличие от `firmware/esp32/` (ESP32‑C3 шлюз → UART → RP2040/STM32), этот проект **не требует отдельного MCU**: команда из WebSocket применяется напрямую к PWM.

## Технологии

- **Язык**: C++23 (C++26 при поддержке тулчейна)
- **Фреймворк**: ESP‑IDF v5.5
- **Цель**: `esp32s3`

## Сборка и прошивка

Из директории проекта:

```bash
cd projects/rc_vehicle/firmware/esp32_s3

# один раз:
idf.py set-target esp32s3

# сборка
idf.py build

# прошивка + монитор
idf.py flash monitor
```

## Подключение (Wi‑Fi + веб)

После старта ESP32‑S3 поднимает открытую Wi‑Fi точку доступа:

- SSID: `RC-Vehicle-XXYY` (по MAC)
- Пароль: нет

Откройте в браузере:

- `http://192.168.4.1`

WebSocket:

- `ws://192.168.4.1:81/ws`

## Конфигурация пинов

Все пины и тайминги задаются в `main/config.hpp`.

По умолчанию используются “простые” GPIO (2–9), но **обязательно** проверьте распиновку вашей платы ESP32‑S3 и при необходимости поменяйте:

- PWM: `PWM_THROTTLE_PIN`, `PWM_STEERING_PIN`
- RC‑in: `RC_IN_THROTTLE_PIN`, `RC_IN_STEERING_PIN`
- SPI (IMU): `SPI_CS_PIN`, `SPI_SCK_PIN`, `SPI_MOSI_PIN`, `SPI_MISO_PIN`

⚠️ Если RC приёмник выдаёт 5V логику — используйте делитель/level‑shifter (ESP32‑S3 = 3.3V max).

## Логика управления

- **RC имеет приоритет** над Wi‑Fi, если сигнал валиден.
- `wifi_active=true`, если команды по WebSocket приходили недавно (`WIFI_CMD_TIMEOUT_MS`).
- **failsafe**: если нет активного источника (RC или Wi‑Fi) дольше `FAILSAFE_TIMEOUT_MS` → выставляется нейтраль.
- **slew‑rate**: applied плавно тянется к commanded.

