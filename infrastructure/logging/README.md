# Инфраструктура логирования

Этот проект содержит конфигурацию стека логирования на основе Grafana Loki, Promtail и Grafana.

## Структура

- `docker-compose.yml` - конфигурация Docker Compose для стека логирования
- `loki-config.yml` - конфигурация Loki (хранилище логов)
- `promtail-config.yml` - конфигурация Promtail (сборщик логов)
- `grafana/` - конфигурация Grafana (веб-интерфейс)

## Быстрый старт

### Запуск

```bash
cd infrastructure/logging
docker-compose -f docker-compose.yml up -d
```

Или из корня проекта:
```bash
make logs-stack-up
```

### Остановка

```bash
cd infrastructure/logging
docker-compose -f docker-compose.yml down
```

Или из корня проекта:
```bash
make logs-stack-down
```

## Доступ

- **Grafana**: http://localhost:3001
  - Логин: `admin`
  - Пароль: `admin` (или значение из `GRAFANA_ADMIN_PASSWORD` в `.env`)
- **Loki API**: http://localhost:3100

## Сеть

Стек логирования использует ту же Docker сеть, что и основной проект (`experiment-tracking-network`), чтобы иметь доступ к логам всех контейнеров.

Подробная документация: [../../README-LOGGING.md](../../README-LOGGING.md)

