# FW-CI1 — Настроить отдельный CI job для тестов прошивки

**Статус:** [x] Выполнено (2026-06-11) — v1: host GTest; сборка ESP-IDF (`make rc-build`) вынесена в follow-up

## Реализация (2026-06-11)

`.github/workflows/firmware-tests.yml` — job `firmware-tests` по конвенциям
существующих workflows (paths-фильтр, `permissions: contents: read`,
checkout@v4). Уточнения к плану ниже:

- **Бинарник тестов — `./build/unit_tests`** (цель `unit_tests` в
  `tests/CMakeLists.txt`), а не `./build/tests`.
- **`make rc-build` (полная сборка ESP-IDF) в v1 не включён**: требует
  тулчейн ESP-IDF v5.x в CI (контейнер espressif/idf, +5-10 мин на job).
  Если нужно — отдельная задача FW-CI2.
- Артефакт — `test-results.xml` (gtest XML), upload-artifact@v4 (v3 deprecated).
- Кэширование CMake/FetchContent отложено: чистая сборка ~2 мин, не горит.

## Описание

В настоящий момент автоматизированное тестирование прошивки (GTest) не интегрировано в GitHub Actions CI pipeline. Это означает, что регрессии в коде прошивки могут попасть в main без сигнала об ошибке.

**Цель:** добавить отдельный job в `.github/workflows/`, который при каждом push'е в `projects/rc_vehicle/firmware/` автоматически:
1. Собирает прошивку (`make rc-build`)
2. Запускает unit-тесты (`./build/tests`)
3. Сообщает результат (зелёный статус или красный с логами)

## Решение

### 1. Создать новый workflow файл

Расположение: `.github/workflows/firmware-tests.yml`

Параметры:
- **Триггеры:** push и PR на `main/develop`, фильтр на изменения в `projects/rc_vehicle/firmware/**`
- **Матрица сборки:** Ubuntu Latest (достаточно одной конфигурации для unit-тестов)
- **Зависимости:** cmake, build-essential (проверить `.devcontainer/Dockerfile` для точного списка)

### 2. Структура job'а

```yaml
firmware-tests:
  runs-on: ubuntu-latest
  steps:
    - uses: actions/checkout@v4
    - name: Install dependencies
      run: |
        sudo apt-get update
        sudo apt-get install -y cmake build-essential [другие зависимости]
    - name: Build firmware tests
      run: |
        cd projects/rc_vehicle/firmware/tests
        cmake -B build
        cmake --build build
    - name: Run tests
      run: ./projects/rc_vehicle/firmware/tests/build/tests
    - name: Upload test results
      if: always()
      uses: actions/upload-artifact@v3
      with:
        name: firmware-test-results
        path: projects/rc_vehicle/firmware/tests/build/
```

### 3. Оптимизация

- **Кэширование:** использовать `actions/cache` для CMake build artifacts
- **Условие срабатывания:** job должен срабатывать только если изменены файлы в `projects/rc_vehicle/firmware/`
- **Уведомления:** на GitHub UI отображать статус (red/green check), опционально отправлять комментарий в PR с результатами

## Критерии приёмки

- [ ] Job создан и доступен в `.github/workflows/firmware-tests.yml`
- [ ] Job срабатывает при push'е с изменениями в `projects/rc_vehicle/firmware/`
- [ ] Job пропускается, если в коммите нет изменений в firmware
- [ ] При успехе: зелёный статус ✓ в GitHub UI (checks)
- [ ] При ошибке в сборке или тестах: красный статус ✗ с логом
- [ ] Логи и артефакты (результаты тестов) сохраняются в GitHub Actions UI
- [ ] Тест локально всегда проходит: `cd projects/rc_vehicle/firmware/tests && cmake -B build && cmake --build build && ./build/tests`

## Ссылки

- GitHub Actions документация: https://docs.github.com/en/actions
- Существующие workflows: `.github/workflows/` (изучить пример Python tests workflow)
- Firmware тесты: `projects/rc_vehicle/firmware/tests/CMakeLists.txt`
- Makefile: `Makefile` (команды `rc-build`, команда сборки тестов)
