# attach-esp32.ps1
# Пробрасывает ESP32-S3 (или другой ESP32) в WSL через usbipd-win.
#
# Использование:
#   PowerShell (от имени администратора) -> .\scripts\attach-esp32.ps1
#
# Требования:
#   - usbipd-win: winget install --interactive --exact dorssel.usbipd-win
#   - WSL2 с Ubuntu запущен

param(
    # VID:PID устройства. По умолчанию — ESP32-S3 встроенный JTAG (303a:1001).
    # Для плат с CP2102: 10c4:ea60
    # Для плат с CH340:  1a86:7523
    [string]$VidPid = "303a:1001",

    # Имя дистрибутива WSL (пусто = дефолтный)
    [string]$WslDistro = ""
)

# Проверка что запущено от администратора
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator
)
if (-not $isAdmin) {
    Write-Host "ОШИБКА: Запустите PowerShell от имени администратора." -ForegroundColor Red
    exit 1
}

# Проверка что usbipd установлен
if (-not (Get-Command usbipd -ErrorAction SilentlyContinue)) {
    Write-Host "ОШИБКА: usbipd не найден." -ForegroundColor Red
    Write-Host "Установите: winget install --interactive --exact dorssel.usbipd-win" -ForegroundColor Yellow
    exit 1
}

# Получить список устройств
Write-Host "Поиск ESP32 (VID:PID=$VidPid)..." -ForegroundColor Cyan
$rawList = usbipd list 2>&1
$matchedLine = $rawList | Select-String -Pattern $VidPid

if (-not $matchedLine) {
    Write-Host "Устройство с VID:PID=$VidPid не найдено." -ForegroundColor Red
    Write-Host ""
    Write-Host "Подключённые USB устройства:" -ForegroundColor Yellow
    usbipd list
    Write-Host ""
    Write-Host "Укажите нужный VID:PID через параметр: .\attach-esp32.ps1 -VidPid 10c4:ea60" -ForegroundColor Yellow
    exit 1
}

# Извлечь BUSID (первая колонка, формат X-Y)
$busId = ($matchedLine.ToString() -split "\s+")[0].Trim()
Write-Host "Найдено: $($matchedLine.ToString().Trim())" -ForegroundColor Green

# Привязать если ещё не привязано (bind идемпотентен)
Write-Host "Привязка устройства $busId..." -ForegroundColor Cyan
usbipd bind --busid $busId 2>&1 | Out-Null

# Подключить в WSL
$attachArgs = @("attach", "--wsl", "--busid", $busId)
if ($WslDistro) {
    $attachArgs += @("--distribution", $WslDistro)
}

Write-Host "Подключение $busId в WSL..." -ForegroundColor Cyan
& usbipd @attachArgs

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "Готово! Устройство доступно в WSL." -ForegroundColor Green
    Write-Host "В WSL выполните:" -ForegroundColor Yellow
    Write-Host "  ls /dev/ttyACM* /dev/ttyUSB*" -ForegroundColor White
    Write-Host "  idf.py -p /dev/ttyACM0 flash monitor" -ForegroundColor White
} else {
    Write-Host ""
    Write-Host "Ошибка при подключении (код: $LASTEXITCODE)." -ForegroundColor Red
    Write-Host "Проверьте что WSL запущен: wsl --list --running" -ForegroundColor Yellow
}
