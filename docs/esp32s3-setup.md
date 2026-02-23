# Инструкция по подключению ESP32-S3

## 1. Общее описание

**ESP32-S3** используется в проекте как Wi-Fi точка доступа, веб-пульт управления и мост для передачи телеметрии в Experiment Service через WebSocket.

### 1.1. Назначение
- Создание Wi-Fi точки доступа для веб-интерфейса управления
- WebSocket сервер для передачи телеметрии и команд управления
- UART мост между веб-интерфейсом и STM32/RP2040 контроллером
- Интеграция с telemetry-cli для отправки данных в Experiment Service

### 1.2. Поддерживаемые платы
- **ESP32-S3 Zero mini** (компактная плата, выбранный вариант)
- **ESP32-S3 DevKit** / **ESP32-S3-WROOM-1** (полноразмерная, рекомендуется для старта)

## 2. Физическое подключение

### 2.1. Питание

**Важно:** ESP32-S3 требует стабильного питания 5V или 3.3V (в зависимости от платы).

#### Вариант 1: Питание от 5V (рекомендуется)
- **Источник:** BEC от ESC или отдельный DC-DC преобразователь 5V 2-3A
- **Подключение:**
  - `5V` → `VIN` или `5V` пин на ESP32-S3
  - `GND` → `GND` на ESP32-S3
- **Требования:**
  - Минимум 2A для стабильной работы
  - Рекомендуется 3A при использовании с сервоприводами
  - Обязательны конденсаторы: **470-1000µF** (low-ESR) + **0.1µF** керамика на шине питания

#### Вариант 2: Питание от USB
- Подходит только для отладки и разработки
- **Не использовать одновременно с внешним 5V!**
- При работе в поле использовать только внешнее питание

#### Вариант 3: Питание от 3.3V
- Если плата поддерживает прямое питание 3.3V
- Подключение: `3.3V` → `3V3` пин
- Требуется стабильный источник 3.3V (например, от регулятора на STM32/RP2040 плате)

### 2.2. UART подключение к STM32/RP2040

ESP32-S3 общается с основным контроллером по UART для передачи команд управления и получения телеметрии.

**Подключение:**
- ESP32-S3 `TX` → STM32/RP2040 `RX` (UART)
- ESP32-S3 `RX` → STM32/RP2040 `TX` (UART)
- `GND` → `GND` (общая земля обязательна!)

**Рекомендуемые пины для ESP32-S3:**
- Для ESP32-S3 Zero mini: используйте выведенные `TX`/`RX` пины
- Для ESP32-S3 DevKit: можно использовать `GPIO43` (TX) и `GPIO44` (RX) или другие свободные UART пины

**Важно:**
- Уровни логики: ESP32-S3 работает на 3.3V
- Если STM32/RP2040 также на 3.3V — прямое подключение
- Если контроллер на 5V — требуется level shifter или делитель напряжения

**Настройки UART:**
- Скорость: `115200` baud (или другая, согласованная с прошивкой STM32/RP2040)
- Формат: 8N1 (8 бит данных, без паритета, 1 стоп-бит)

### 2.3. Дополнительные подключения (опционально)

#### Индикация статуса
- LED подключение к свободному GPIO (например, `GPIO2` или `GPIO8`)
- Используется для индикации Wi-Fi статуса, подключения и т.д.

#### Кнопка сброса/перезагрузки
- Обычно уже есть на плате
- Для программного сброса можно использовать `EN` пин

### 2.4. Важные замечания по пинам

**Избегайте использования:**
- `GPIO0` (BOOT) — используется для режима загрузки
- `GPIO19/GPIO20` — USB пины (если используется USB)
- Пины с пометкой "strapping" — могут влиять на режим загрузки

**Рекомендуемые свободные GPIO:**
- `GPIO1-8`, `GPIO9-10`, `GPIO11-21` (кроме USB и strapping)
- Конкретный набор зависит от модели платы

## 3. Настройка WSL для разработки на Windows

WSL (Windows Subsystem for Linux) — рекомендуемая среда для разработки прошивок ESP32 на Windows. Она обеспечивает полностью Linux-совместимое окружение, где ESP-IDF работает без проблем совместимости.

### 3.1. Требования

- Windows 10 версии 2004+ (Build 19041+) или Windows 11
- Права администратора для установки WSL и usbipd-win
- USB-кабель для подключения ESP32-S3

### 3.2. Установка WSL2 и Ubuntu

#### Шаг 1: Включение WSL2

Откройте **PowerShell** от имени администратора и выполните:

```powershell
# Установка WSL с Ubuntu по умолчанию
wsl --install

# Если WSL уже установлен, но нужна Ubuntu:
wsl --install -d Ubuntu-24.04

# Убедиться что используется WSL2 (а не WSL1)
wsl --set-default-version 2
```

После установки система потребует перезагрузки. После перезагрузки откроется терминал Ubuntu для первоначальной настройки (задайте имя пользователя и пароль).

#### Шаг 2: Проверка установки

```powershell
# Список установленных дистрибутивов и их версий
wsl --list --verbose
```

Вывод должен показывать `VERSION 2` для Ubuntu:

```
  NAME            STATE           VERSION
* Ubuntu-24.04    Running         2
```

#### Шаг 3: Обновление системы внутри WSL

Откройте Ubuntu (через меню Пуск или `wsl` в терминале) и выполните:

```bash
sudo apt update && sudo apt upgrade -y
```

### 3.3. Проброс USB-устройства в WSL (usbipd-win)

По умолчанию WSL не имеет доступа к USB-устройствам Windows. Для прошивки ESP32 нужен проброс USB через **usbipd-win**.

#### Шаг 1: Установка usbipd-win на Windows

```powershell
# Через winget (рекомендуется)
winget install --interactive --exact dorssel.usbipd-win
```

Либо скачайте `.msi` установщик с [GitHub Releases](https://github.com/dorssel/usbipd-win/releases).

После установки **перезапустите PowerShell** (usbipd добавляется в PATH).

#### Шаг 2: Установка клиента usbip в WSL

В терминале Ubuntu:

```bash
sudo apt install linux-tools-generic hwdata -y
sudo update-alternatives --install /usr/local/bin/usbip usbip \
    /usr/lib/linux-tools/*-generic/usbip 20
```

#### Шаг 3: Привязка и подключение ESP32

Подключите ESP32-S3 через USB к компьютеру. Затем в **PowerShell от имени администратора**:

```powershell
# Посмотреть список USB устройств
usbipd list
```

Пример вывода:

```
BUSID  VID:PID    DEVICE                          STATE
2-3    303a:1001  USB JTAG/serial debug unit       Not shared
2-4    046d:c52b  USB Input Device                 Not shared
```

ESP32-S3 обычно определяется как `USB JTAG/serial debug unit` (VID `303a`) или `CP210x`/`CH340` (в зависимости от платы).

```powershell
# Привязать устройство (один раз после установки usbipd)
usbipd bind --busid 2-3

# Подключить устройство в WSL (каждый раз при новом подключении)
usbipd attach --wsl --busid 2-3
```

Проверить в WSL:

```bash
ls /dev/ttyACM* /dev/ttyUSB*
# Должно появиться /dev/ttyACM0 или /dev/ttyUSB0
```

#### Шаг 4: Отключение устройства

Когда USB устройство больше не нужно в WSL:

```powershell
usbipd detach --busid 2-3
```

#### Скрипт автоматизации (attach-esp32.ps1)

Создайте файл `scripts/attach-esp32.ps1` в корне репозитория для удобного подключения:

```powershell
# scripts/attach-esp32.ps1
# Запуск: PowerShell от имени администратора -> .\scripts\attach-esp32.ps1

$esp32VidPid = "303a:1001"  # ESP32-S3 JTAG. Измените под вашу плату

Write-Host "Поиск ESP32-S3..." -ForegroundColor Cyan
$devices = usbipd list | Select-String $esp32VidPid

if (-not $devices) {
    Write-Host "ESP32-S3 не найден. Проверьте подключение USB." -ForegroundColor Red
    exit 1
}

# Извлечь BUSID (формат: "2-3")
$busId = ($devices -split "\s+")[0].Trim()
Write-Host "Найдено устройство: $devices" -ForegroundColor Green

# Привязать если ещё не привязано
usbipd bind --busid $busId 2>$null

# Подключить в WSL
Write-Host "Подключение $busId в WSL..." -ForegroundColor Cyan
usbipd attach --wsl --busid $busId

if ($LASTEXITCODE -eq 0) {
    Write-Host "Готово! Устройство доступно в WSL." -ForegroundColor Green
    Write-Host "В WSL выполните: ls /dev/ttyACM* /dev/ttyUSB*" -ForegroundColor Yellow
} else {
    Write-Host "Ошибка подключения. Запустите PowerShell от имени администратора." -ForegroundColor Red
}
```

### 3.4. Настройка прав на последовательный порт в WSL

По умолчанию `/dev/ttyACM0` требует прав root. Добавьте пользователя в группу `dialout`:

```bash
# Добавить пользователя в группу dialout (один раз)
sudo usermod -aG dialout $USER

# Применить без перезапуска сессии
newgrp dialout
```

Для постоянного автоматического назначения прав создайте правило udev:

```bash
# Создать файл правила udev для ESP32-S3 (VID 303a)
sudo tee /etc/udev/rules.d/99-esp32.rules << 'EOF'
# ESP32-S3 (USB JTAG)
SUBSYSTEM=="tty", ATTRS{idVendor}=="303a", MODE="0666", GROUP="dialout"
# ESP32 с CP2102 (Silicon Labs)
SUBSYSTEM=="tty", ATTRS{idVendor}=="10c4", MODE="0666", GROUP="dialout"
# ESP32 с CH340
SUBSYSTEM=="tty", ATTRS{idVendor}=="1a86", MODE="0666", GROUP="dialout"
EOF

sudo udevadm control --reload-rules
sudo udevadm trigger
```

### 3.5. Интеграция с VS Code (Remote - WSL)

VS Code с расширением Remote - WSL позволяет разрабатывать прямо в WSL с полным IntelliSense и доступом к инструментам Linux.

#### Установка расширения

```bash
# В PowerShell
code --install-extension ms-vscode-remote.remote-wsl
```

#### Открытие проекта в WSL

Вариант 1 — из терминала WSL:

```bash
# Перейти в папку проекта и открыть VS Code
cd ~/esp/my_esp32s3_project
code .
```

Вариант 2 — из VS Code:
- Нажмите `F1` → `Remote-WSL: New WSL Window`
- Или нажмите на иконку `><` в левом нижнем углу → `Connect to WSL`

#### Рекомендуемые расширения для C/ESP-IDF в WSL

```bash
# Устанавливаются в WSL окружение
code --install-extension ms-vscode.cpptools
code --install-extension ms-vscode.cmake-tools
code --install-extension espressif.esp-idf-extension
```

Расширение `espressif.esp-idf-extension` предоставляет интеграцию с ESP-IDF прямо в VS Code: сборка, прошивка, монитор порта через GUI.

### 3.6. Проверка настройки WSL

Последовательность проверки перед разработкой:

```bash
# 1. Проверить что ESP32 виден в WSL
ls /dev/ttyACM* /dev/ttyUSB*

# 2. Проверить что ESP-IDF активирован
echo $IDF_PATH
# Должно вывести: /home/<user>/esp/esp-idf

# 3. Если IDF_PATH пустой — активировать окружение
. ~/esp/esp-idf/export.sh

# 4. Проверить версию idf.py
idf.py --version

# 5. Тестовое подключение к ESP32
idf.py -p /dev/ttyACM0 monitor
# Ctrl+] для выхода из монитора
```

### 3.7. Типовой рабочий процесс в WSL

```
1. Подключить ESP32-S3 по USB к Windows
2. PowerShell (admin): usbipd attach --wsl --busid <BUSID>
   или запустить scripts/attach-esp32.ps1
3. WSL: . ~/esp/esp-idf/export.sh  (если не добавлено в ~/.bashrc)
4. WSL: idf.py build
5. WSL: idf.py -p /dev/ttyACM0 flash monitor
6. После работы: PowerShell: usbipd detach --busid <BUSID>
```

**Добавьте активацию ESP-IDF в `~/.bashrc` (один раз) чтобы не запускать вручную:**

```bash
echo '. ~/esp/esp-idf/export.sh' >> ~/.bashrc
```

---

## 4. Настройка прошивки

### 4.1. Установка инструментов разработки

#### ESP-IDF 6.0 (нативный фреймворк Espressif, рекомендуется)

ESP-IDF даёт полный контроль над железом и производительностью — предпочтительный вариант для production-прошивок.

**Требования:**
- Git
- Python 3.9+
- CMake 3.24+

**Установка в WSL (Ubuntu) — рекомендуется на Windows:**

```bash
# Зависимости
sudo apt update && sudo apt install -y \
    git wget flex bison gperf python3 python3-pip python3-venv \
    cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0

# Клонирование ESP-IDF v6.0
mkdir -p ~/esp
cd ~/esp
git clone -b v6.0 --recursive https://github.com/espressif/esp-idf.git

# Установка инструментов для ESP32-S3
cd ~/esp/esp-idf
./install.sh esp32s3

# Активация окружения (добавить в ~/.bashrc для постоянного использования)
. ~/esp/esp-idf/export.sh
```

**Установка в Windows (PowerShell, нативно):**

```powershell
# Клонирование ESP-IDF v6.0
mkdir $HOME\esp
cd $HOME\esp
git clone -b v6.0 --recursive https://github.com/espressif/esp-idf.git

# Установка инструментов
cd $HOME\esp\esp-idf
.\install.ps1 esp32s3

# Активация окружения
.$HOME\esp\esp-idf\export.ps1
```

**Создание и сборка проекта:**

```bash
# Создать проект из шаблона
cd ~/esp
idf.py create-project my_esp32s3_project
cd my_esp32s3_project

# Выбрать целевую платформу
idf.py set-target esp32s3

# Настройка (menuconfig открывает TUI-конфигуратор)
idf.py menuconfig

# Сборка
idf.py build

# Прошивка и мониторинг (/dev/ttyUSB0 — порт устройства)
idf.py -p /dev/ttyUSB0 flash monitor
```

**Минимальный `CMakeLists.txt` для проекта:**

```cmake
cmake_minimum_required(VERSION 3.24)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(my_esp32s3_project)
```

**Структура проекта:**

```
my_esp32s3_project/
├── CMakeLists.txt
├── sdkconfig              # генерируется после menuconfig
└── main/
    ├── CMakeLists.txt
    └── main.c
```

> **Доступ к USB-порту в WSL:** см. раздел [3.3](#33-проброс-usb-устройства-в-wsl-usbipd-win) выше.

---

#### Arduino IDE
1. Установите [Arduino IDE](https://www.arduino.cc/en/software)
2. Добавьте поддержку ESP32:
   - File → Preferences → Additional Boards Manager URLs
   - Добавьте: `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. Tools → Board → Boards Manager → найдите "esp32" → установите "esp32 by Espressif Systems"
4. Выберите плату: Tools → Board → ESP32 Arduino → ESP32S3 Dev Module

#### PlatformIO
1. Установите [PlatformIO](https://platformio.org/)
2. Создайте проект:
   ```bash
   pio project init --board esp32-s3-devkitc-1
   ```
3. Настройте `platformio.ini`:
   ```ini
   [env:esp32-s3-devkitc-1]
   platform = espressif32
   board = esp32-s3-devkitc-1
   framework = arduino
   monitor_speed = 115200
   ```

### 4.2. Базовые настройки прошивки

#### Wi-Fi точка доступа
```cpp
#include <WiFi.h>
#include <WebSocketsServer.h>

const char* ssid = "RC-Vehicle-AP";
const char* password = "rc12345678";  // минимум 8 символов

WiFiServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

void setup() {
  Serial.begin(115200);

  // Настройка UART для связи с STM32
  Serial1.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);

  // Создание Wi-Fi точки доступа
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);

  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  // Запуск WebSocket сервера
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  // Запуск HTTP сервера для веб-интерфейса
  server.begin();
}
```

#### WebSocket обработка
```cpp
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\n", num);
      break;

    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[%u] Connected from %d.%d.%d.%d\n", num, ip[0], ip[1], ip[2], ip[3]);
      }
      break;

    case WStype_TEXT:
      // Обработка команд управления
      handleCommand((char*)payload);
      break;

    default:
      break;
  }
}
```

### 4.3. Протокол UART с STM32/RP2040

Реализация протокола согласно `docs/interfaces_protocols.md`:

```cpp
// Формат кадра: AA 55 | VER | TYPE | LEN_LO LEN_HI | PAYLOAD... | CRC16_LO CRC16_HI

#define FRAME_PREFIX_0 0xAA
#define FRAME_PREFIX_1 0x55
#define PROTOCOL_VERSION 0x01

#define TYPE_COMMAND 0x01
#define TYPE_TELEM   0x02

void sendCommand(uint16_t seq, float throttle, float steering) {
  uint8_t buffer[64];
  int idx = 0;

  // Префикс
  buffer[idx++] = FRAME_PREFIX_0;
  buffer[idx++] = FRAME_PREFIX_1;

  // Версия и тип
  buffer[idx++] = PROTOCOL_VERSION;
  buffer[idx++] = TYPE_COMMAND;

  // Длина payload (SEQ + THR + STEER + FLAGS = 2 + 2 + 2 + 1 = 7)
  uint16_t payload_len = 7;
  buffer[idx++] = payload_len & 0xFF;
  buffer[idx++] = (payload_len >> 8) & 0xFF;

  // Payload
  buffer[idx++] = seq & 0xFF;
  buffer[idx++] = (seq >> 8) & 0xFF;

  int16_t thr_i16 = (int16_t)(throttle * 32767.0f);
  buffer[idx++] = thr_i16 & 0xFF;
  buffer[idx++] = (thr_i16 >> 8) & 0xFF;

  int16_t steer_i16 = (int16_t)(steering * 32767.0f);
  buffer[idx++] = steer_i16 & 0xFF;
  buffer[idx++] = (steer_i16 >> 8) & 0xFF;

  buffer[idx++] = 0; // FLAGS

  // CRC16 (считается по VER..PAYLOAD)
  uint16_t crc = calculateCRC16(buffer + 2, idx - 2);
  buffer[idx++] = crc & 0xFF;
  buffer[idx++] = (crc >> 8) & 0xFF;

  Serial1.write(buffer, idx);
}
```

### 4.4. Формат телеметрии для WebSocket

Отправка телеметрии в формате, совместимом с `telemetry-cli`:

```cpp
void sendTelemetry(uint64_t ts_ms, float imu_ax, float imu_ay, float imu_az,
                   float imu_gx, float imu_gy, float imu_gz,
                   float throttle, float steering, bool rc_ok, bool wifi_ok) {
  DynamicJsonDocument doc(512);

  doc["type"] = "telem";
  doc["ts_ms"] = ts_ms;

  JsonObject link = doc.createNestedObject("link");
  link["active_source"] = rc_ok ? "rc" : "wifi";
  link["rc_ok"] = rc_ok;
  link["wifi_ok"] = wifi_ok;

  JsonObject imu = doc.createNestedObject("imu");
  imu["ax"] = imu_ax;
  imu["ay"] = imu_ay;
  imu["az"] = imu_az;
  imu["gx"] = imu_gx;
  imu["gy"] = imu_gy;
  imu["gz"] = imu_gz;

  JsonObject act = doc.createNestedObject("act");
  act["throttle"] = throttle;
  act["steering"] = steering;

  String output;
  serializeJson(doc, output);
  webSocket.broadcastTXT(output);
}
```

## 5. Подключение к сети и тестирование

### 5.1. Первый запуск

1. **Загрузите прошивку** в ESP32-S3 через USB
2. **Откройте Serial Monitor** (115200 baud)
3. **Проверьте вывод:**
   - Должно появиться сообщение о создании Wi-Fi точки доступа
   - Должен быть указан IP адрес (обычно `192.168.4.1`)

### 5.2. Подключение к Wi-Fi точке доступа

1. На вашем компьютере/телефоне найдите Wi-Fi сеть с именем `RC-Vehicle-AP` (или как настроено в прошивке)
2. Подключитесь с паролем (по умолчанию `rc12345678`)
3. Откройте браузер и перейдите по адресу `http://192.168.4.1` (или IP из Serial Monitor)

### 5.3. Тестирование WebSocket

#### Через браузер (консоль разработчика)
```javascript
const ws = new WebSocket('ws://192.168.4.1/ws');

ws.onopen = () => {
  console.log('Connected');
  // Отправка команды
  ws.send(JSON.stringify({
    type: 'cmd',
    throttle: 0.2,
    steering: -0.1,
    seq: 1
  }));
};

ws.onmessage = (event) => {
  const data = JSON.parse(event.data);
  if (data.type === 'telem') {
    console.log('Telemetry:', data);
  }
};
```

#### Через telemetry-cli

Создайте конфигурационный файл `configs/esp32_ws.yaml`:

```yaml
experiment_service:
  base_url: "http://localhost:8002"
  sensor_token: "YOUR_SENSOR_TOKEN"
  timeout_s: 10

target:
  sensor_id: "00000000-0000-0000-0000-000000000000"
  run_id: null
  capture_session_id: null
  meta:
    vehicle_type: "rc_car"
    controller: "esp32"
    link_protocol: "ws"

batch:
  max_readings: 400
  flush_interval_ms: 300

source:
  type: "esp32_ws"
  url: "ws://192.168.4.1/ws"
  reconnect_delay_ms: 1000
```

Запустите telemetry-cli:
```bash
cd projects/telemetry_cli
telemetry-cli --config configs/esp32_ws.yaml
```

## 6. Интеграция с Experiment Service

### 6.1. Получение sensor_token

1. Зарегистрируйтесь в Experiment Service через Auth Service
2. Создайте сенсор через API или веб-интерфейс
3. Получите `sensor_token` для созданного сенсора
4. Укажите токен в конфигурации `telemetry-cli`

### 6.2. Настройка метаданных

В конфигурации `telemetry-cli` укажите метаданные устройства:

```yaml
target:
  meta:
    vehicle_type: "rc_car"  # или "rc_plane", "rc_drone"
    controller: "esp32"
    board: "esp32s3"
    fw_version: "0.1.0"
    link_protocol: "ws"
    device_id: "vehicle-01"
```

Эти метаданные будут добавлены к каждой точке телеметрии.

### 6.3. Проверка работы

1. Убедитесь, что Experiment Service запущен и доступен
2. Запустите `telemetry-cli` с правильной конфигурацией
3. Проверьте логи telemetry-cli на наличие ошибок
4. В Experiment Service должны появляться записи телеметрии

## 7. Устранение неполадок

### 7.1. ESP32-S3 не запускается

**Симптомы:** нет индикации, не видно в Serial Monitor

**Решения:**
- Проверьте питание (должно быть стабильное 5V или 3.3V)
- Проверьте подключение GND
- Убедитесь, что не подаёте одновременно USB и внешнее питание
- Проверьте, что кнопка BOOT не зажата
- Попробуйте нажать кнопку RESET

### 7.2. Wi-Fi точка доступа не создаётся

**Симптомы:** не видно сети в списке Wi-Fi

**Решения:**
- Проверьте Serial Monitor на наличие ошибок
- Убедитесь, что антенна подключена (для некоторых плат)
- Проверьте, что SSID и пароль соответствуют требованиям (пароль минимум 8 символов)
- Попробуйте перезагрузить ESP32-S3

### 7.3. WebSocket не подключается

**Симптомы:** telemetry-cli не может подключиться, ошибки в логах

**Решения:**
- Проверьте IP адрес ESP32-S3 (должен быть `192.168.4.1` по умолчанию)
- Убедитесь, что вы подключены к Wi-Fi точке доступа ESP32-S3
- Проверьте URL в конфигурации: `ws://192.168.4.1/ws`
- Проверьте, что WebSocket сервер запущен в прошивке
- Проверьте firewall на компьютере

### 7.4. UART не работает

**Симптомы:** нет связи между ESP32-S3 и STM32/RP2040

**Решения:**
- Проверьте подключение TX→RX и RX→TX (перекрёстное!)
- Убедитесь, что общая земля (GND) подключена
- Проверьте уровни логики (3.3V vs 5V)
- Проверьте скорость UART (должна совпадать на обеих сторонах)
- Используйте Serial Monitor для отладки UART на ESP32-S3

### 7.5. Телеметрия не отправляется в Experiment Service

**Симптомы:** telemetry-cli работает, но данные не появляются в сервисе

**Решения:**
- Проверьте `sensor_token` в конфигурации
- Убедитесь, что Experiment Service запущен и доступен по указанному URL
- Проверьте логи telemetry-cli на наличие ошибок HTTP
- Убедитесь, что формат телеметрии соответствует ожидаемому (поле `type: "telem"`)

### 7.6. ESP32 не виден в WSL

**Симптомы:** `/dev/ttyACM0` или `/dev/ttyUSB0` не появляется в WSL

**Решения:**
- Убедитесь, что usbipd-win установлен на Windows
- Запустите PowerShell **от имени администратора**
- Проверьте `usbipd list` — устройство должно быть в статусе `Shared` или `Not shared`
- Выполните `usbipd bind --busid <BUSID>` а затем `usbipd attach --wsl --busid <BUSID>`
- Убедитесь, что WSL запущен (`wsl --list --running`)
- После `attach` проверьте в WSL: `dmesg | tail -20` (должно быть сообщение об обнаружении ttyACM/ttyUSB)

### 7.7. Permission denied при обращении к /dev/ttyACM0

**Симптомы:** `idf.py flash` завершается с ошибкой прав доступа

**Решения:**
```bash
# Временное решение (действует до перезапуска WSL)
sudo chmod 666 /dev/ttyACM0

# Постоянное решение — добавить в группу dialout
sudo usermod -aG dialout $USER
newgrp dialout

# Или создать правило udev (см. раздел 3.4)
```

## 8. Рекомендации по безопасности

### 8.1. Wi-Fi точка доступа

- **Измените пароль по умолчанию** на более сложный
- Используйте WPA2 (реализовано по умолчанию в ESP32)
- Рассмотрите возможность отключения точки доступа в production и использование подключения к существующей Wi-Fi сети

### 8.2. WebSocket

- В production добавьте аутентификацию для WebSocket соединений
- Ограничьте доступ к веб-интерфейсу (например, по IP)
- Используйте HTTPS/WSS для защищённого соединения (требует сертификата)

### 8.3. UART протокол

- Используйте CRC для проверки целостности данных
- Реализуйте таймауты для обнаружения потери связи
- Добавьте проверку версии протокола

## 9. Дополнительные ресурсы

### 9.1. Документация

- [ESP32-S3 Technical Reference Manual](https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf)
- [ESP32 Arduino Core Documentation](https://docs.espressif.com/projects/arduino-esp32/en/latest/)
- [WebSocketsServer Library](https://github.com/Links2004/arduinoWebSockets)
- [usbipd-win GitHub](https://github.com/dorssel/usbipd-win) — USB passthrough для WSL
- [WSL USB Connect Docs](https://learn.microsoft.com/en-us/windows/wsl/connect-usb) — официальная документация Microsoft

### 9.2. Связанные документы проекта

- `docs/telemetry-rc-stm32.md` — формат телеметрии для Experiment Service
- `docs/telemetry-cli-ts.md` — документация telemetry-cli
- `projects/rc_vehicle/docs/interfaces_protocols.md` — протоколы UART и WebSocket
- `projects/rc_vehicle/docs/bom_ru.md` — список компонентов и рекомендации по выбору платы

### 9.3. Примеры кода

Примеры прошивок для ESP32-S3 можно найти в:
- `projects/rc_vehicle/firmware/esp32/` (когда будет добавлено)

## 10. Чек-лист подключения

- [ ] ESP32-S3 плата выбрана и приобретена
- [ ] Питание настроено (5V 2-3A с конденсаторами)
- [ ] UART подключен к STM32/RP2040 (TX→RX, RX→TX, GND)
- [ ] **WSL (Windows):** WSL2 + Ubuntu установлены
- [ ] **WSL (Windows):** usbipd-win установлен, ESP32 привязан (`usbipd bind`)
- [ ] **WSL (Windows):** пользователь добавлен в группу `dialout`
- [ ] ESP-IDF установлен и активирован (`echo $IDF_PATH`)
- [ ] Прошивка загружена в ESP32-S3
- [ ] Wi-Fi точка доступа создаётся (проверено через Serial Monitor)
- [ ] Подключение к точке доступа работает
- [ ] WebSocket сервер отвечает (проверено через браузер)
- [ ] Телеметрия отправляется (проверено через telemetry-cli)
- [ ] Данные появляются в Experiment Service
- [ ] UART связь с STM32/RP2040 работает

## 11. Следующие шаги

После успешного подключения ESP32-S3:

1. **Разработка веб-интерфейса** для управления RC-моделью
2. **Интеграция с IMU** через STM32/RP2040
3. **Реализация failsafe** логики
4. **Оптимизация протокола UART** для минимальной задержки
5. **Добавление логирования** и диагностики
