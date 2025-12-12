# Логирование с Grafana Loki

Этот проект использует Grafana Loki для централизованного сбора и просмотра логов всех сервисов через веб-интерфейс.

## Быстрый старт

### Автоматический запуск (Development режим)

Стек логирования **автоматически запускается** при `docker-compose up` в development режиме (когда используется `docker-compose.override.yml`):

```bash
docker-compose up
```

Grafana будет доступна на http://localhost:3001 сразу после запуска.

### Ручной запуск (Production или отдельно)

Если нужно запустить стек логирования отдельно:

```bash
make logs-stack-up
```

Или вручную:
```bash
docker-compose -f docker-compose.yml -f docker-compose.logging.yml up -d
```

### 2. Доступ к Grafana

- **URL**: http://localhost:3001
- **Логин**: `admin`
- **Пароль**: `admin` (или значение из `GRAFANA_ADMIN_PASSWORD` в `.env`)

### 3. Loki API

Loki API доступен на http://localhost:3100, но это **API сервер**, а не веб-интерфейс.

**Важно**: Для просмотра логов используйте **Grafana** (http://localhost:3001), а не Loki API напрямую.

Доступные эндпоинты Loki API:
- `http://localhost:3100/ready` - проверка готовности
- `http://localhost:3100/metrics` - метрики Prometheus
- `http://localhost:3100/loki/api/v1/labels` - список меток
- `http://localhost:3100/loki/api/v1/query` - запрос логов
- `http://localhost:3100/loki/api/v1/query_range` - запрос логов за период

Пример запроса через API:
```bash
curl "http://localhost:3100/loki/api/v1/labels"
```

### 4. Просмотр логов

1. Откройте Grafana: http://localhost:3001
2. Перейдите в **Explore** (иконка компаса в левом меню)
3. Выберите datasource **Loki**
4. Используйте LogQL запросы для фильтрации логов

## Примеры LogQL запросов

### Все логи конкретного сервиса
```
{service="experiment-service"}
```

### Все логи по имени контейнера
```
{container=~"experiment-.*"}
```

### Только ошибки
```
{service=~".+"} |= "error" |= "ERROR" |= "Error" |= "exception"
```

### Логи auth-proxy
```
{service="auth-proxy"}
```

### Логи PostgreSQL
```
{container="experiment-postgres"}
```

### Комбинация фильтров
```
{service="experiment-service"} |= "error" | json | level="ERROR"
```

## Компоненты стека

### Loki
- Хранилище логов
- API: http://localhost:3100
- Конфигурация: `loki-config.yml`

### Promtail
- Сборщик логов из Docker контейнеров
- Автоматически обнаруживает контейнеры через Docker socket
- Конфигурация: `promtail-config.yml`

### Grafana
- Веб-интерфейс для визуализации
- URL: http://localhost:3001
- Автоматически настроен datasource Loki

## Управление стеком

```bash
# Запуск
make logs-stack-up

# Остановка
make logs-stack-down

# Перезапуск
make logs-stack-restart

# Просмотр статуса
docker-compose -f docker-compose.yml -f docker-compose.logging.yml ps
```

## Настройка

### Переменные окружения (в `.env`)

```bash
# Порт Grafana
GRAFANA_PORT=3001

# Порт Loki
LOKI_PORT=3100

# Учетные данные Grafana
GRAFANA_ADMIN_USER=admin
GRAFANA_ADMIN_PASSWORD=admin

# Разрешить анонимный доступ (true/false)
GRAFANA_ANONYMOUS_ENABLED=false
```

### Фильтрация логов в Promtail

По умолчанию Promtail собирает логи только контейнеров проекта `product_dev_course`.

Чтобы собирать логи всех контейнеров, удалите или закомментируйте строки 33-35 в `promtail-config.yml`:

```yaml
# - source_labels: ['__meta_docker_container_label_com_docker_compose_project']
#   regex: 'product_dev_course'
#   action: keep
```

## Хранение данных

Данные хранятся в Docker volumes:
- `experiment-loki-data` - логи Loki
- `experiment-grafana-data` - настройки и дашборды Grafana

Для очистки данных:
```bash
docker-compose -f docker-compose.yml -f docker-compose.logging.yml down -v
```

## Troubleshooting

### Логи не появляются в Grafana

1. Проверьте, что Promtail запущен:
   ```bash
   docker-compose -f docker-compose.yml -f docker-compose.logging.yml ps promtail
   ```

2. Проверьте логи Promtail:
   ```bash
   docker-compose -f docker-compose.yml -f docker-compose.logging.yml logs promtail
   ```

3. Проверьте, что Loki доступен:
   ```bash
   curl http://localhost:3100/ready
   ```

### Не могу подключиться к Grafana

1. Проверьте, что контейнер запущен:
   ```bash
   docker-compose -f docker-compose.yml -f docker-compose.logging.yml ps grafana
   ```

2. Проверьте логи:
   ```bash
   docker-compose -f docker-compose.yml -f docker-compose.logging.yml logs grafana
   ```

3. Убедитесь, что порт 3001 не занят:
   ```bash
   netstat -tuln | grep 3001
   ```

## Дополнительные ресурсы

- [Grafana Loki Documentation](https://grafana.com/docs/loki/latest/)
- [LogQL Query Language](https://grafana.com/docs/loki/latest/logql/)
- [Promtail Configuration](https://grafana.com/docs/loki/latest/clients/promtail/configuration/)
