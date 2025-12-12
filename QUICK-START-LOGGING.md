# Быстрый старт: Grafana для просмотра логов

## Запуск

### Автоматически (Development режим)

Стек логирования запускается автоматически при:
```bash
docker-compose up
```

### Вручную (если нужно запустить отдельно)

```bash
make logs-stack-up
```

## Доступ

- **Grafana** (веб-интерфейс): http://localhost:3001
  - **Логин**: `admin`
  - **Пароль**: `admin`

> **Примечание**: Loki API доступен на http://localhost:3100, но это API-сервер без веб-интерфейса. Для просмотра логов используйте Grafana.

## Просмотр логов

1. Откройте http://localhost:3001
2. Перейдите в **Explore** (иконка компаса слева)
3. Выберите datasource **Loki**
4. Введите запрос, например:
   - `{service="experiment-service"}` - логи сервиса
   - `{container=~"experiment-.*"}` - все контейнеры проекта
   - `{service=~".+"} |= "error"` - только ошибки

## Остановка

```bash
make logs-stack-down
```

Подробная документация: [README-LOGGING.md](README-LOGGING.md)
