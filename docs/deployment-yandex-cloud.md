# Деплой в Yandex Cloud

## Архитектура

```
                    Internet
                       |
                  [Static IP]
                       |
              +--------+--------+
              |   Compute VM    |
              |  (Docker Comp.) |
              |                 |
              |  +-----------+  |          +-------------------+
              |  | Portal    |--+---:80    |  Yandex Managed   |
              |  | (nginx)   |  |          |  PostgreSQL       |
              |  +-----------+  |          |                   |
              |  +-----------+  |          |  - auth_db        |
              |  | Auth      |--+---:8080  |  - experiment_db  |
              |  | Proxy BFF |  |          |  + TimescaleDB    |
              |  +-----------+  |          +--------+----------+
              |  +-----------+  |                   |
              |  | Auth Svc  |--+---(внутр.)--------+
              |  +-----------+  |
              |  +-----------+  |
              |  | Experiment|--+---(внутр.)
              |  | Service   |  |
              |  +-----------+  |
              |  +-----------+  |
              |  | Telemetry |--+---:8003
              |  | Ingest    |  |
              |  +-----------+  |
              |  +-----------+  |
              |  | Sensor    |--+---:8082
              |  | Simulator |  |
              |  +-----------+  |
              |  +-----------+  |
              |  | Grafana   |--+---:3001
              |  | + Loki    |  |
              |  +-----------+  |
              +-----------------+
                       |
              [Yandex Container
               Registry (CR)]
```

## Используемые сервисы Yandex Cloud

| Сервис | Для чего | Примерная стоимость |
|--------|----------|---------------------|
| **Compute Cloud** | VM для Docker Compose (2 vCPU, 4 GB) | ~2500 руб/мес |
| **Managed PostgreSQL** | БД с TimescaleDB (s2.micro, 20 GB SSD) | ~4000 руб/мес |
| **Container Registry** | Хранение Docker-образов | ~100 руб/мес |
| **VPC + Static IP** | Сеть и статический адрес | ~300 руб/мес |
| **Итого** | | **~7000 руб/мес** |

> Для экономии можно использовать прерываемую VM (`vm_preemptible = true`) — в 3 раза дешевле, но может быть остановлена.

## Пререквизиты

1. **Аккаунт Yandex Cloud** с привязанным биллингом
2. **Yandex Cloud CLI** (`yc`): [инструкция](https://cloud.yandex.ru/docs/cli/quickstart)
3. **Terraform** >= 1.5: [установка](https://developer.hashicorp.com/terraform/install)
4. **Docker** для локальной сборки образов (опционально)
5. **SSH-ключ** для доступа к VM

## Быстрый старт

### 1. Настройка Yandex Cloud CLI

```bash
# Установка
curl -sSL https://storage.yandexcloud.net/yandexcloud-yc/install.sh | bash

# Инициализация (следуйте интерактивным подсказкам)
yc init

# Проверка
yc config list
```

### 2. Создание инфраструктуры (Terraform)

```bash
cd infrastructure/yandex-cloud

# Копируем и заполняем переменные
cp terraform.tfvars.example terraform.tfvars
# Отредактируйте terraform.tfvars — укажите cloud_id, folder_id, пароли

# Инициализация
terraform init

# Предпросмотр
terraform plan

# Создание ресурсов (~10-15 минут)
terraform apply
```

Terraform создаст:
- VPC + подсеть + security groups
- Managed PostgreSQL кластер с двумя БД
- Container Registry
- Compute VM с Docker
- Service accounts для VM и CI/CD

### 3. Сохранение outputs

```bash
# Public IP
terraform output vm_public_ip

# Registry URL
terraform output container_registry_url

# PostgreSQL хост
terraform output pg_cluster_host

# CI ключ (для GitHub Secrets)
terraform output -json ci_sa_key_private
```

### 4. Настройка VM

```bash
# Скопировать скрипт настройки
scp scripts/setup-vm.sh deploy@<VM_IP>:~

# Запустить
ssh deploy@<VM_IP> 'bash ~/setup-vm.sh'
```

### 5. Первый деплой

```bash
# Скопировать конфигурацию
scp docker-compose.prod.yml deploy@<VM_IP>:/opt/experiment-tracking/
scp env.production.example deploy@<VM_IP>:/opt/experiment-tracking/.env
scp -r infrastructure/logging/ deploy@<VM_IP>:/opt/experiment-tracking/infrastructure/

# На VM — отредактировать .env
ssh deploy@<VM_IP>
nano /opt/experiment-tracking/.env
# Заполнить: DATABASE_URL, JWT_SECRET, CR_REGISTRY, пароли

# Запустить
cd /opt/experiment-tracking
docker compose -f docker-compose.prod.yml pull
docker compose -f docker-compose.prod.yml up -d
```

### 6. Проверка

```bash
# Статус контейнеров
docker compose -f docker-compose.prod.yml ps

# Health checks
curl http://<VM_IP>/           # Portal
curl http://<VM_IP>:8080/health  # Auth Proxy
curl http://<VM_IP>:8003/health  # Telemetry Ingest
```

## CI/CD (GitHub Actions)

### Настройка GitHub Secrets

Добавьте в Settings > Secrets and Variables > Actions:

| Secret | Откуда | Описание |
|--------|--------|----------|
| `YC_REGISTRY_ID` | `terraform output container_registry_id` | ID Container Registry |
| `YC_SA_JSON_KEY` | `terraform output -json ci_sa_key_private` | JSON-ключ SA для пуша образов |
| `VM_HOST` | `terraform output vm_public_ip` | IP-адрес VM |
| `VM_USER` | `deploy` | SSH пользователь |
| `VM_SSH_PRIVATE_KEY` | Содержимое `~/.ssh/id_ed25519` | SSH приватный ключ |

### Как работает пайплайн

```
Push в main
    |
    v
[Тесты] backend + frontend (параллельно)
    |
    v (тесты прошли)
[Build & Push] 6 образов в Container Registry (матрица)
    |
    v
[Deploy] SSH на VM -> pull -> up -d -> health check
```

### Ручной деплой

```bash
VM_HOST=84.201.xxx.xxx REGISTRY_ID=crp... ./scripts/deploy.sh
```

## Миграции БД

При первом запуске миграции должны применяться автоматически (если сервисы это поддерживают).

Для ручного запуска:

```bash
ssh deploy@<VM_IP>
cd /opt/experiment-tracking

# Auth Service миграции
docker compose -f docker-compose.prod.yml exec auth-service \
  python -m auth_service.migrate

# Experiment Service миграции
docker compose -f docker-compose.prod.yml exec experiment-service \
  python -m experiment_service.migrate
```

## Мониторинг

### Grafana

Доступна по адресу `http://<VM_IP>:3001`

- Логин: `admin` / пароль из `.env`
- Datasource Loki уже настроен
- Дашборды из `infrastructure/logging/grafana/dashboards/` подключены

### Логи через CLI

```bash
ssh deploy@<VM_IP>
cd /opt/experiment-tracking

# Все логи
docker compose -f docker-compose.prod.yml logs -f

# Конкретный сервис
docker compose -f docker-compose.prod.yml logs -f auth-service

# Последние 100 строк
docker compose -f docker-compose.prod.yml logs --tail=100 experiment-service
```

## SSL/TLS (опционально)

Для HTTPS рекомендуется использовать Caddy или Certbot + Nginx:

```bash
# На VM
sudo apt-get install -y certbot
sudo certbot certonly --standalone -d your-domain.com

# Затем обновить nginx-конфигурацию experiment-portal
# для проксирования через HTTPS
```

Альтернатива — использовать Yandex Application Load Balancer с управляемым сертификатом.

## Обновление

### Обновление приложения

Автоматически при пуше в `main` через GitHub Actions, или вручную:

```bash
VM_HOST=<ip> REGISTRY_ID=<id> ./scripts/deploy.sh
```

### Обновление инфраструктуры

```bash
cd infrastructure/yandex-cloud
terraform plan    # проверить изменения
terraform apply   # применить
```

## Откат

```bash
ssh deploy@<VM_IP>
cd /opt/experiment-tracking

# Откатить на конкретный тег (sha коммита)
sed -i 's|^IMAGE_TAG=.*|IMAGE_TAG=abc1234|' .env
docker compose -f docker-compose.prod.yml pull
docker compose -f docker-compose.prod.yml up -d
```

## Удаление инфраструктуры

```bash
cd infrastructure/yandex-cloud
terraform destroy
```

> ВНИМАНИЕ: удалит все ресурсы, включая базу данных. Сделайте бэкап перед удалением.

## Бэкапы

Yandex Managed PostgreSQL создаёт автоматические бэкапы. Для ручного:

```bash
# Через yc CLI
yc managed-postgresql cluster list-backups <cluster-id>

# Восстановление
yc managed-postgresql cluster restore \
  --backup-id <backup-id> \
  --name experiment-tracking-pg-restored \
  --environment PRODUCTION
```

## Troubleshooting

| Проблема | Решение |
|----------|---------|
| Контейнер не стартует | `docker compose logs <service>` |
| Нет подключения к БД | Проверить Security Group, `sslmode=verify-full`, сертификат |
| 502 Bad Gateway | Подождать 30-60 сек, проверить healthcheck |
| Нет места на диске | `docker system prune -a` |
| Образы не пуллятся | `yc container registry configure-docker` |
