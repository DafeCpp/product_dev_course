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
              |  +-----------+  |          |  - config_db      |
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
| **Compute Cloud** | VM для Docker Compose (2 vCPU, 6 GB) | зависит от актуального тарифа |
| **Managed PostgreSQL** | БД с TimescaleDB (s2.micro, 20 GB SSD) | ~4000 руб/мес |
| **Container Registry** | Хранение Docker-образов | ~100 руб/мес |
| **VPC + Static IP** | Сеть и статический адрес | ~300 руб/мес |
| **Итого** | | зависит от актуальных тарифов |

> Прерываемую VM (`vm_preemptible = true`) можно использовать только в dev/staging.
> Production VM должна оставаться непрерываемой (`vm_preemptible = false`).

## Пререквизиты

1. **Аккаунт Yandex Cloud** с привязанным биллингом
2. **Права в каталоге:** учётная запись, от которой выполняется `terraform apply` (OAuth через `yc init` или сервисный аккаунт), должна иметь в каталоге роль **Администратор** (или как минимум роли для создания ресурсов + **Управление доступом к ресурсам** / `resource-manager.admin`). Иначе создание IAM-привязок для VM и CI завершится ошибкой *Permission denied*.
3. **Yandex Cloud CLI** (`yc`): [инструкция](https://cloud.yandex.ru/docs/cli/quickstart)
4. **Terraform** >= 1.5: [установка](https://developer.hashicorp.com/terraform/install)
5. **jq** для проверки сохранённого Terraform plan
6. **Docker** для локальной сборки образов (опционально)
7. **SSH-ключ** для доступа к VM

## Установка пререквизитов (Linux)

Если чего-то не хватает, выполните по шагам.

**Yandex Cloud CLI (`yc`):**

```bash
curl -sSL https://storage.yandexcloud.net/yandexcloud-yc/install.sh | bash
# Добавить в PATH (или перезайти в терминал):
export PATH="$HOME/yandex-cloud/bin:$PATH"
yc init   # один раз: облако, каталог, OAuth
yc config list
```

**Terraform (>= 1.5):**

- **Вариант A — APT (Ubuntu/Debian):**
  [Официальная инструкция](https://developer.hashicorp.com/terraform/install) — добавьте репозиторий HashiCorp и установите `terraform`.
- **Вариант B — бинарник:**
  Скачайте архив для Linux AMD64 с [releases.hashicorp.com/terraform](https://releases.hashicorp.com/terraform/), распакуйте и поместите `terraform` в каталог из `PATH` (например `~/bin` или `/usr/local/bin`).

```bash
terraform version
```

**SSH-ключ:** если нет `~/.ssh/id_ed25519.pub`:

```bash
ssh-keygen -t ed25519 -C "deploy-yc" -f ~/.ssh/id_ed25519 -N ""
```

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

**Настройка провайдера (рекомендуется, если registry.terraform.io недоступен по гео).**
Файл `~/.terraformrc` должен лежать в домашнем каталоге пользователя (например `/home/user/` или `C:\Users\user\`). Если раньше был конфиг с реестром HashiCorp, сохраните его: `mv ~/.terraformrc ~/.terraformrc.old`. Затем создайте или откройте `~/.terraformrc` и добавьте:

```hcl
provider_installation {
  network_mirror {
    url     = "https://terraform-mirror.yandexcloud.net/"
    include = ["registry.terraform.io/*/*"]
  }
  direct {
    exclude = ["registry.terraform.io/*/*"]
  }
}
```

Так Terraform будет ставить провайдер Yandex с [официального зеркала Yandex Cloud](https://yandex.cloud/en/docs/terraform/quickstart) без обращения к registry.terraform.io.

```bash
(
set -euo pipefail

cd infrastructure/yandex-cloud

# Копируем и заполняем переменные
cp terraform.tfvars.example terraform.tfvars
# Отредактируйте terraform.tfvars — укажите cloud_id, folder_id, пароли

# Инициализация
terraform init

# Если используется .terraform.lock.hcl и провайдер не подтянулся, привяжите lock к зеркалу:
# terraform providers lock -net-mirror=https://terraform-mirror.yandexcloud.net -platform=linux_amd64 -platform=windows_amd64 -platform=darwin_arm64 yandex-cloud/yandex

# Saved plan содержит sensitive values: закрываем права и храним в ignored .terraform/.
umask 077
TFPLAN=.terraform/tfplan
trap 'rm -f "$TFPLAN"' EXIT
terraform plan -out="$TFPLAN"
terraform show "$TFPLAN"

# Создание ресурсов (~10-15 минут)
terraform apply "$TFPLAN"
rm -f "$TFPLAN"
trap - EXIT
)
```

Terraform создаст:
- VPC + подсеть + security groups
- Managed PostgreSQL кластер с тремя БД
- Container Registry
- Compute VM с Docker
- Service accounts для VM и CI/CD
- Приватный Object Storage bucket и отдельный bucket-scoped service account для артефактов

### 3. Сохранение outputs

```bash
# Public IP
terraform output vm_public_ip

# Registry URL
terraform output container_registry_url

# PostgreSQL хост
terraform output pg_cluster_host

# Готовые sensitive DSN для production .env.
# Команда -raw выводит пароль в терминал: не сохраняйте вывод в shell history или git.
terraform output -raw auth_database_url
terraform output -raw experiment_database_url
terraform output -raw config_database_url
terraform output -raw script_database_url

# CI ключ (для GitHub Secrets)
terraform output -raw ci_sa_key_json

# Object Storage: несекретные значения
terraform output -raw artifacts_s3_endpoint_url
terraform output -raw artifacts_bucket_name

# Object Storage: static key. -raw выводит секреты в терминал;
# переносите их сразу в production .env, не записывая в файлы внутри репозитория.
terraform output -raw artifacts_s3_access_key
terraform output -raw artifacts_s3_secret_key
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
scp scripts/validate-production-env.sh deploy@<VM_IP>:/opt/experiment-tracking/
scp env.production.example deploy@<VM_IP>:/opt/experiment-tracking/.env
scp -r infrastructure/logging/ deploy@<VM_IP>:/opt/experiment-tracking/infrastructure/

# На VM — отредактировать .env
ssh deploy@<VM_IP>
nano /opt/experiment-tracking/.env
# Заменить все PASSWORD / CHANGE_ME / GENERATE_* / YOUR_* и доменные заглушки.
# Оставить внутренние runtime URL на Compose DNS:
# REDIS_URL=redis://redis:6379/0
# TELEMETRY_BROKER_URL=redis://redis:6379/0
# CONFIG_CLIENT_URL=http://config-service:8005
# script-service доступен только через auth-proxy: http://script-service:8004
# S3_ENDPOINT_URL=https://storage.yandexcloud.net
# S3_PUBLIC_ENDPOINT_URL=https://storage.yandexcloud.net
# S3_BUCKET=<terraform output -raw artifacts_bucket_name>
# S3_ACCESS_KEY и S3_SECRET_KEY — sensitive Terraform outputs
# S3_PRESIGN_EXPIRE_SECONDS=3600

# Pre-flight выполняется до любых действий с работающим стеком.
cd /opt/experiment-tracking
chmod 700 validate-production-env.sh
./validate-production-env.sh .env docker-compose.prod.yml
docker compose --env-file .env -f docker-compose.prod.yml config --quiet

# Запустить только после успешного pre-flight
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
curl http://127.0.0.1:8004/health # Script Service (internal)

# Redis доступен только внутри app network и должен отвечать PONG.
docker compose -f docker-compose.prod.yml exec -T redis redis-cli ping
# В колонке PORTS у redis не должно быть опубликованного 6379.
docker compose -f docker-compose.prod.yml ps redis
```

## CI/CD (GitHub Actions)

Используется модель веток **main** и **develop**: разработка ведётся в `develop`, релизы выходят из `main` по тегам. Подробнее: [branching-model.md](branching-model.md).

### Аутентификация в Container Registry: сервисный аккаунт (SA)

В [документации Yandex Cloud](https://cloud.yandex.ru/docs/container-registry/operations/authentication) в основном описана аутентификация **как пользователь**: OAuth-токен, IAM-токен, Docker Credential helper (`yc container registry configure-docker`). Для **CI/CD и для VM** в этом проекте используется аутентификация **по ключу сервисного аккаунта (SA)** — без браузера и без хранения пользовательского OAuth.

- **Где используется:** GitHub Actions (пуш образов в реестр), при необходимости — на VM для `docker pull` (если настроен `yc` с ключом SA).
- **Как работает:** вход в реестр `cr.yandex` с логином **`json_key`** и паролем — **полным JSON-ключом** сервисного аккаунта (поля `id`, `service_account_id`, `private_key`, `created_at`). В Terraform для этого есть output **`ci_sa_key_json`** (`terraform output -raw ci_sa_key_json`). Output `ci_sa_key_private` выдаёт ключ в формате PEM — для логина в реестр он не подходит. Альтернатива: создать ключ вручную и взять JSON: `yc iam key create --service-account-name <name> --output key.json`.
- **Роли:** сервисному аккаунту CI нужна роль **Пользователь образа** (push/pull) на реестр или каталог; SA VM — только **Пользователь образа** (pull). Подробнее: [Управление доступом в Container Registry](https://cloud.yandex.ru/docs/container-registry/security/).
- **Официальная инструкция по входу по ключу SA:** [Аутентификация с помощью ключа сервисного аккаунта](https://cloud.yandex.ru/docs/container-registry/operations/authentication#sa-json) (раздел *sa-json* на странице аутентификации).

### Настройка GitHub Secrets

Секреты используются в workflow «Release (Build & Deploy)»: часть — для входа в Container Registry и пуша образов, часть — для SSH-деплоя на VM.

**Где добавлять:**

- **Repository secrets** (Settings → Secrets and variables → Actions): удобнее всего добавить **все** пять секретов сюда — тогда они доступны workflow без настройки Environment. Имена: `YC_REGISTRY_ID`, `YC_SA_JSON_KEY`, `VM_HOST`, `VM_USER`, `VM_SSH_PRIVATE_KEY`.
- **Либо** секреты для реестра — в **Environment secrets** окружения `production` (Settings → Environments → создать `production` → Environment secrets): `YC_REGISTRY_ID`, `YC_SA_JSON_KEY`. Остальные (`VM_HOST`, `VM_USER`, `VM_SSH_PRIVATE_KEY`) — в Repository secrets.

| Secret | Откуда | Формат и примечания |
|--------|--------|----------------------|
| **YC_REGISTRY_ID** | Локально в каталоге Terraform: `terraform output -raw container_registry_id` | Одна строка, например `crpj69t4sk1kem3mb9er`. Repository secrets или Environment `production`. |
| **YC_SA_JSON_KEY** | Локально: `terraform output -raw ci_sa_key_json` | **Важно:** нужен именно **JSON** ключа, не PEM. Команда `terraform output -raw ci_sa_key_private` выдаёт ключ в формате PEM (текст с `-----BEGIN PRIVATE KEY-----`) — для логина в реестр он **не подходит**. Используйте `terraform output -raw ci_sa_key_json` — вывод будет в формате JSON одной строкой (`id`, `service_account_id`, `created_at`, `private_key`). Скопировать целиком в значение секрета. Если при логине появляется *Password required* или *Password is invalid - must be JSON key* — см. [ниже](#github-actions-error-password-required). |
| **VM_HOST** | Локально: `terraform output -raw vm_public_ip` | Публичный IP VM, например `84.201.xxx.xxx`. Добавить в **Repository secrets**. |
| **VM_USER** | Фиксированное значение | Строка `deploy` — пользователь на VM, созданный Terraform. Добавить в **Repository secrets**. |
| **VM_SSH_PRIVATE_KEY** | Файл приватного ключа, которым подключаетесь к VM по SSH | **Содержимое** файла (не путь). Обычно `cat ~/.ssh/id_ed25519` — скопировать весь вывод, включая строки `-----BEGIN ... KEY-----` и `-----END ... KEY-----`. Если ключ создавался по шагу «Быстрый старт» (ssh-keygen), используйте тот же ключ, публичная часть которого передана в `ssh_public_key` в Terraform. Добавить в **Repository secrets**. |

**Как получить значения из Terraform (после `terraform apply`):**

```bash
cd infrastructure/yandex-cloud

# ID реестра (одна строка)
terraform output -raw container_registry_id

# Ключ SA в формате JSON для реестра (скопировать полностью в секрет YC_SA_JSON_KEY)
# Не использовать ci_sa_key_private — это PEM, для docker login json_key нужен именно JSON
terraform output -raw ci_sa_key_json

# Публичный IP VM
terraform output -raw vm_public_ip
```

**Проверка:** после добавления секретов создание тега `v*` на `main` должно запускать workflow; шаг «Login to Yandex Container Registry» использует `YC_REGISTRY_ID` и `YC_SA_JSON_KEY`, шаг деплоя по SSH — `VM_HOST`, `VM_USER`, `VM_SSH_PRIVATE_KEY`.

### Как работает пайплайн

Сборка и деплой в Yandex Cloud запускаются **только при создании тега** вида `v*` (например `v1.0.0`) на ветке `main`. При каждом push в `main` и `develop` выполняются только тесты и линтеры (CI).

```
Push тега v* (например v1.0.0) на main
    |
    v
[Тесты] backend + frontend (параллельно)
    |
    v (тесты прошли)
[Build & Push] 6 образов в Container Registry (теги: v1.0.0, latest)
    |
    v
[Deploy] SSH на VM -> IMAGE_TAG=v1.0.0 -> pull -> up -d -> health check
    |
    v
[Create Release] GitHub Release с сгенерированными release notes
```

### Первый релиз (v1.0.0)

1. Убедиться, что ветка `develop` актуальна и CI зелёный.
2. Слить `develop` в `main` (через PR или локально).
3. Создать и запушить тег:
   ```bash
   git checkout main && git pull origin main
   git tag -a v1.0.0 -m "Release v1.0.0"
   git push origin v1.0.0
   ```
4. В GitHub Actions запустится workflow «Release (Build & Deploy)»; после успешного деплоя появится запись в Releases.

### Ручной деплой (без CI)

```bash
VM_HOST=84.201.xxx.xxx REGISTRY_ID=crp... ./scripts/deploy.sh [v1.0.0]
```
Если тег не указан, используется короткий SHA текущего коммита.

## Миграции БД и инициализация

Миграции применяются автоматически на каждом деплое одноразовыми (one-shot) сервисами
в `docker-compose.prod.yml`. Каждый из них выполняет `python -m bin.migrate`, отрабатывает
до конца и завершается; соответствующий рантайм-сервис стартует только после успешного
завершения своего миграционного джоба (`condition: service_completed_successfully`).

| One-shot джоб | База | Блокирует старт |
|---|---|---|
| `auth-migrate` | `auth_db` | `auth-service` |
| `config-migrate` | `config_db` | `config-service` |
| `script-migrate` | `script_db` | `script-service` |
| `experiment-migrate` | `experiment_db` | `experiment-service` |
| `telemetry-ingest-migrate` | `experiment_db` | `telemetry-ingest-service` |

telemetry-ingest использует **ту же** `experiment_db`, что и experiment-service, и обе
службы пишут в общую таблицу `schema_migrations` (ключ — имя файла миграции, поэтому
`005_idempotency_reservation` и `005_sensor_error_log` не конфликтуют). Чтобы джобы не
гонялись за `CREATE TABLE IF NOT EXISTS schema_migrations`, `telemetry-ingest-migrate`
запускается строго после `experiment-migrate`.

Миграции идемпотентны: повторный прогон на актуальной схеме печатает `No pending migrations.`
и ничего не меняет, поэтому джобы безопасно выполняются на каждом релизе.

Проверить, что миграции применились:

```bash
ssh deploy@<VM_IP>
cd /opt/experiment-tracking

docker compose -f docker-compose.prod.yml logs experiment-migrate
docker compose -f docker-compose.prod.yml logs telemetry-ingest-migrate
docker compose -f docker-compose.prod.yml logs script-migrate
```

Ручной прогон (например, если джоб упал и был пропущен):

```bash
docker compose -f docker-compose.prod.yml run --rm experiment-migrate
docker compose -f docker-compose.prod.yml run --rm telemetry-ingest-migrate
docker compose -f docker-compose.prod.yml run --rm script-migrate
```

Базы и пользователи (`experiment_db` / `experiment_user` и др.) создаются Terraform —
см. `infrastructure/yandex-cloud/database.tf`. Миграции их не создают.

Создание первого администратора — отдельный одноразовый шаг после успешного
deploy; миграции его не выполняют. Не сохраняйте bootstrap credentials в
Terraform, production `.env` или GitHub Actions secrets. Используйте canonical
процедуру с передачей пароля через stdin из
[инструкции по инициализации администратора](admin-initialization.md#production-yandex-cloud).

## Чеклист подключения нового сервиса к production

Новый backend-сервис нельзя считать готовым к production только после добавления в
`docker-compose.prod.yml`. Перед merge и первым релизом пройдите весь checklist:

- **Test/build:** сервис присутствует в CI test matrix и release build/push matrix;
  production Docker image действительно содержит runtime-код и миграции.
- **Compose runtime:** runtime-сервис добавлен в `docker-compose.prod.yml`, имеет
  healthcheck, restart/resource policy, внутреннюю сеть и только необходимые внешние порты.
- **Миграции:** для сервиса с `migrations/` существует one-shot `*-migrate`;
  runtime зависит от него через `service_completed_successfully`.
- **Terraform DB:** добавлены DB/user, password variable с `sensitive = true`,
  корректный owner/extensions и usable sensitive DSN output. Перед apply сохраните и
  прочитайте plan: production VM не должна удаляться или заменяться.
- **Применение инфраструктуры:** Terraform-ресурсы реально применены, а не только
  описаны в коде; повторный `terraform plan` не показывает неожиданный drift.
- **Runtime env:** каждый обязательный ключ используется в compose через
  `${VAR:?message}`, добавлен в `env.production.example` и вручную доставлен в
  `/opt/experiment-tracking/.env` без вывода секрета в логи.
- **Pre-flight:** `validate-production-env.sh` и
  `docker compose --env-file .env -f docker-compose.prod.yml config --quiet` проходят
  на VM до первого `compose down`, `pull` или `up`.
- **Маршрутизация:** proxy target, auth/RBAC, CORS и health-based dependencies обновлены.
- **Диагностика:** сервис включён в deploy status/failure logs и централизованный сбор
  логов; сообщение об ошибке не раскрывает значения env.
- **Smoke:** migrate-job завершился, сервис healthy, запрос через публичный маршрут
  проходит; негативный pre-flight с удалённым обязательным ключом падает без остановки
  уже работающего стека.
- **Документация:** runbook описывает получение инфраструктурных outputs, доставку env,
  ручной deploy, rollback и восстановление после ошибки.

Полный автоматический drift-check между manifest, dev/prod compose, build matrix,
миграциями и Terraform остаётся предметом LOS-112.

## Object Storage: smoke-тест и ротация ключа

MinIO используется только в локальном `docker-compose.yml`. Production
`experiment-service` обращается к приватному Yandex Object Storage bucket через
`https://storage.yandexcloud.net`; браузер получает короткоживущие presigned URL.

После `terraform apply` и deploy проверьте полный путь через UI или API:

1. запросить presigned upload URL и выполнить `PUT` тестового файла с тем же
   `Content-Type`, который был указан при создании URL;
2. запросить presigned download URL и сверить содержимое файла;
3. удалить artifact через API и убедиться, что последующий download возвращает 404;
4. проверить логи `experiment-service`, не выводя значения ключей.

Bucket приватный, `force_destroy = false`, а runtime service account получает
`storage.editor` через bucket IAM binding — доступа к другим bucket каталога у него нет.
CORS origins задаются переменной `artifacts_cors_allowed_origins`; wildcard origin в
production не используйте.

### Ротация static access key

Секрет static key доступен Terraform только при создании. Для плановой ротации:

```bash
cd infrastructure/yandex-cloud
umask 077
terraform plan -replace=yandex_iam_service_account_static_access_key.artifacts_sa_key -out=.terraform/rotate-artifacts-key.tfplan
terraform apply .terraform/rotate-artifacts-key.tfplan
terraform output -raw artifacts_s3_access_key
terraform output -raw artifacts_s3_secret_key
rm -f .terraform/rotate-artifacts-key.tfplan
```

Сразу замените оба значения в `/opt/experiment-tracking/.env`, выполните pre-flight и
пересоздайте только `experiment-service`:

```bash
./validate-production-env.sh .env docker-compose.prod.yml
docker compose --env-file .env -f docker-compose.prod.yml up -d --no-deps --force-recreate experiment-service
docker compose --env-file .env -f docker-compose.prod.yml ps experiment-service
```

Старый ключ удаляется самим Terraform replacement. Не помещайте ключи в shell scripts,
GitHub Actions logs, task comments или файлы репозитория.

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

Релиз и деплой выполняются при создании тега `v*` на `main` (см. выше и [branching-model.md](branching-model.md)). Для повторного деплоя той же версии без CI:

```bash
VM_HOST=<ip> REGISTRY_ID=<id> ./scripts/deploy.sh v1.0.0
```

### Обновление инфраструктуры

```bash
(
set -euo pipefail

cd infrastructure/yandex-cloud
terraform fmt -check
terraform validate

# Saved plan содержит sensitive values: закрываем права и храним в ignored .terraform/.
umask 077
TFPLAN=.terraform/tfplan
trap 'rm -f "$TFPLAN"' EXIT
terraform plan -out="$TFPLAN"
terraform show "$TFPLAN"

# Проверить, что план не удаляет и не заменяет ресурсы.
terraform show -json "$TFPLAN" | jq -e \
  '[.resource_changes[] | select(.change.actions | index("delete"))] | length == 0'

# Production VM должна оставаться без изменений.
terraform show -json "$TFPLAN" | jq -e \
  '[.resource_changes[] | select(.address == "yandex_compute_instance.app") | .change.actions] == [["no-op"]]'

# Применять только сохранённый и проверенный план.
terraform apply "$TFPLAN"
rm -f "$TFPLAN"
trap - EXIT
)
```

Если любая из проверок завершилась ошибкой, `terraform apply` выполнять нельзя.
Добавление новых ресурсов допустимо, но действия `delete` и `replace` должны быть
разобраны и согласованы отдельно. Не используйте `-target`, удаление ресурса из
state или `-replace`, чтобы обойти защиту production VM.

Изменение актуального образа семейства COI игнорируется для уже созданной VM:
новый `image_id` не должен превращать обычное обновление инфраструктуры в
пересоздание машины.

### Контролируемая замена production VM

`yandex_compute_instance.app` защищена через `prevent_destroy`. Если VM
действительно требуется заменить:

1. Создайте отдельную задачу и PR с причиной замены и планом миграции.
2. Сохраните `.env`, сертификаты и данные persistent Docker volumes в защищённое
   хранилище и проверьте восстановление.
3. Запланируйте maintenance window и способ отката на старую VM.
4. В отдельном PR временно измените lifecycle-защиту и приложите проверенный
   Terraform plan, в котором replacement является единственным ожидаемым
   destructive action.
5. После миграции верните `prevent_destroy = true` и убедитесь, что новый plan
   не содержит изменений.

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

Обычный `terraform destroy` и `make infra-destroy` намеренно не могут удалить
production VM: ресурс защищён через `prevent_destroy`. Полный teardown выполняйте
только как отдельную контролируемую операцию:

1. Создайте задачу и reviewed PR, обосновывающие полное удаление окружения.
2. Сделайте и проверьте бэкапы Managed PostgreSQL, `.env`, сертификатов и
   persistent Docker volumes.
3. В этом PR временно удалите `prevent_destroy` из
   `yandex_compute_instance.app`, выполните `terraform plan -destroy` и
   добавьте текстовое резюме результата. Saved plan не публикуйте: он может
   содержать секреты. Не удаляйте VM из Terraform state для обхода защиты.
4. После merge выполните teardown с явным подтверждением:

   ```bash
   make infra-destroy CONFIRM_PRODUCTION_DESTROY=destroy-production
   ```

5. Отдельным PR верните `prevent_destroy = true`, чтобы следующее окружение снова
   создавалось с защитой.

> ВНИМАНИЕ: teardown удалит все управляемые ресурсы, включая базу данных.

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
| **terraform init: "Invalid provider registry host" / "does not offer a Terraform provider registry"** | См. ниже [Terraform: недоступен registry](#terraform-недоступен-registry). |
| **terraform apply: "Failed to Update IAM Policy" / "Permission denied"** | Либо выдать учётной записи Terraform роль **Администратор** в каталоге (Права доступа). Либо отключить создание IAM-привязок: в `terraform.tfvars` задать `manage_folder_iam = false`, затем вручную в консоли выдать SA `container-registry.images.puller` (для VM) и `container-registry.images.pusher`/`puller` (для CI). |
| **user name 'postgres' is not allowed** | В Yandex Managed PostgreSQL имя `postgres` зарезервировано. Используется переменная `pg_admin_username` (по умолчанию `cluster_admin`). Если в state уже был пользователь с именем postgres: `terraform state rm yandex_mdb_postgresql_user.admin`, затем снова `terraform apply`. |
| Контейнер не стартует | `docker compose logs <service>` |
| **redis unhealthy / consumers не стартуют** | Проверить `docker compose -f docker-compose.prod.yml logs redis` и `docker compose -f docker-compose.prod.yml exec -T redis redis-cli ping`. Убедиться, что `REDIS_URL` и `TELEMETRY_BROKER_URL` равны `redis://redis:6379/0`; Redis не должен публиковать порт на VM. |
| **dependency failed: container auth-service is unhealthy** | На VM проверить: 1) `AUTH_DATABASE_URL` в `.env` и доступность БД (Security Group, сертификат `./certs/yandex-ca.pem`); 2) `JWT_SECRET` задан; 3) `docker compose -f docker-compose.prod.yml logs auth-service` — по логам увидеть ошибку (подключение к БД, SSL и т.д.). При падении деплоя в CI шаг «Show auth-service logs on deploy failure» выведет логи. |
| **permission denied to create extension "pgcrypto"** | Расширение pgcrypto должно создаваться при создании БД (Terraform или суперпользователем). В `database.tf` для `auth_db`, `experiment_db`, `config_db` и `script_db` добавлены блоки `extension { name = "pgcrypto" }`. Для **уже существующего** кластера: выполнить `terraform apply` — Terraform добавит расширение. Либо один раз от имени cluster_admin: `psql ... -d auth_db -c "CREATE EXTENSION IF NOT EXISTS pgcrypto;"` (и то же для experiment_db / config_db / script_db). |
| **script-service не стартует / proxy отдаёт 502** | Проверить `SCRIPT_DATABASE_URL` в `.env`, затем `docker compose -f docker-compose.prod.yml logs script-migrate script-service`. Убедиться, что `script-migrate` завершился успешно и порт `8004` не опубликован наружу. |
| **config-роли не появились после релиза (RBAC config-service не работает)** | Миграция `auth-service/003_config_rbac.sql` применяется one-shot сервисом `auth-migrate` на деплое. Убедитесь, что `auth-migrate` отработал успешно (`docker compose -f docker-compose.prod.yml logs auth-migrate`). Для config-service миграции применяет `config-migrate`; база `config_db` и пользователь `config_user` должны быть созданы Terraform (`database.tf`). |
| **experiment-service: functionality not supported under the current "apache" license** (TimescaleDB) | В Yandex MDB используется TimescaleDB с лицензией Apache 2.0: компрессия и continuous aggregates недоступны. Миграции 001/002 принудительно пропускают эти шаги (DO ... EXCEPTION). Сервис должен стартовать; экспорт телеметрии с агрегацией 1m на Yandex недоступен (нет материализованного представления `telemetry_1m`). |
| Нет подключения к БД | Проверить Security Group, `sslmode=verify-full`, сертификат |
| 502 Bad Gateway | Подождать 30-60 сек, проверить healthcheck |
| Нет места на диске | Краткосрочно: на VM `docker system prune -a`. Надолго: увеличить диск ВМ — в `terraform.tfvars` задать `vm_disk_size_gb = 90` (или больше), затем `terraform apply` (ВМ может перезапуститься). По умолчанию теперь 50 ГБ. |
| **docker pull: "unable to get credentials" / "error getting credentials"** (образы cr.yandex) | На VM не настроена авторизация в Container Registry. См. ниже [Docker: авторизация в CR на VM](#docker-авторизация-в-cr-на-vm). |
| **docker pull: "repository ... not found"** (cr.yandex/...) | Образы ещё не загружены в реестр. Нужно один раз собрать и отправить их: с **локальной машины** (где есть исходный код и Docker) выполнить `VM_HOST=<IP> REGISTRY_ID=<id> ./scripts/deploy.sh`. На локальной машине предварительно: `yc container registry configure-docker` (авторизация для пуша). |
| **GitHub Actions: "Error: Password required"** (docker/login-action, cr.yandex) | Секрет `YC_SA_JSON_KEY` пустой или не виден job'у. См. ниже [GitHub Actions: Error Password required](#github-actions-error-password-required). |
| **GitHub Actions: "Password is invalid - must be JSON key"** (cr.yandex) | В `YC_SA_JSON_KEY` должен быть **полный** JSON ключа SA (одна строка). См. [GitHub Actions: Password is invalid (JSON key)](#github-actions-password-is-invalid-json-key). |
| Образы не пуллятся | После настройки авторизации: `yc container registry configure-docker` (на VM под пользователем, для которого настроен yc). |

### GitHub Actions: Error Password required

Ошибка **"Error: Password required"** в шаге «Login to Yandex Container Registry» означает, что в `docker/login-action` приходит пустой пароль — секрет `YC_SA_JSON_KEY` не подставлен или пуст.

**Что проверить:**

1. **Имя секрета** — ровно `YC_SA_JSON_KEY` (регистр важен).
2. **Где лежит секрет:** job «Build & Push» использует `environment: production`. Секрет должен быть либо в **Repository secrets** (Settings → Secrets and variables → Actions), либо в **Environment secrets** окружения **production** (Settings → Environments → production → Environment secrets). Если окружение `production` только создали и секреты добавляли в Environment — убедитесь, что сохранили именно в Environment secrets этого окружения. Проще всего добавить `YC_SA_JSON_KEY` и `YC_REGISTRY_ID` в **Repository secrets** — тогда они точно доступны.
3. **Значение не пустое и именно JSON:** использовать `terraform output -raw ci_sa_key_json` (не `ci_sa_key_private` — тот выдаёт PEM, реестр его не принимает):
   ```bash
   cd infrastructure/yandex-cloud
   terraform output -raw ci_sa_key_json
   ```
   Вывод — одна строка JSON (начинается с `{"id":...`). Скопировать **целиком** и вставить в значение секрета `YC_SA_JSON_KEY`.
4. **Обновить секрет:** после правки в GitHub нажмите «Update secret». Затем перезапустите failed workflow (Re-run all jobs) или создайте тег заново.

### GitHub Actions: Password is invalid (JSON key)

Ошибка **"Password is invalid - must be JSON key"** при логине в `cr.yandex` означает, что реестр не принимает значение секрета `YC_SA_JSON_KEY`: нужен именно **полный JSON ключа** сервисного аккаунта в формате Yandex ([документация](https://cloud.yandex.ru/docs/container-registry/operations/authentication#sa-json)).

**Типичные причины:**

1. **Вставлен не весь ключ** — только поле `private_key` (PEM) или обрезанный JSON. Нужен **целиком** объект с полями `id`, `service_account_id`, `private_key`, `created_at`.
2. **Ключ с переносами строк** — при копировании из файла или «красивого» JSON реестр может отклонить формат. Нужна **одна строка** без переносов внутри.

**Как исправить:**

Заново получить ключ **в формате JSON** (не PEM) и вставить его в секрет:

```bash
cd infrastructure/yandex-cloud
terraform output -raw ci_sa_key_json
```

Команда `ci_sa_key_private` выдаёт ключ в формате PEM (строки `-----BEGIN PRIVATE KEY-----` и т.д.) — для входа в реестр с логином `json_key` нужен **полный JSON**, его даёт только `ci_sa_key_json`. Скопировать **весь** вывод (начинается с `{"id":"...`, заканчивается `...}`) и вставить в значение секрета `YC_SA_JSON_KEY` в GitHub. Не добавлять переводы строк в начале/конце.

После обновления секрета перезапустить workflow (Re-run all jobs).

### Docker: авторизация в CR на VM

Ошибка *unable to get credentials* / *error getting credentials* при `docker compose pull` для образов `cr.yandex/...` означает, что на VM не настроен доступ к Yandex Container Registry. Скрипт `setup-vm.sh` вызывает `yc container registry configure-docker`, но для работы нужна авторизация **yc** — на «голой» VM её нет.

**Что сделать:**

1. **Проверить права SA VM.** В консоли Yandex Cloud убедиться, что сервисному аккаунту VM (имя вида `experiment-tracking-vm-sa`) выдана роль **Пользователь образа** (или `container-registry.images.puller`) на реестр или каталог (если при `manage_folder_iam = false` вы давали права вручную).

2. **Создать ключ сервисного аккаунта VM и настроить yc на VM.**
   На своей машине (где есть `yc` с правами на каталог):
   ```bash
   # Узнать ID сервисного аккаунта VM (из Terraform или консоли)
   yc iam service-account list

   # Создать ключ (сохранить key.json, не коммитить в git)
   yc iam key create --service-account-name experiment-tracking-vm-sa --output key-vm-sa.json
   ```
   Скопировать ключ на VM и войти по SSH:
   ```bash
   scp key-vm-sa.json deploy@<VM_IP>:/opt/experiment-tracking/
   ssh deploy@<VM_IP>
   ```
   На VM:
   ```bash
   cd /opt/experiment-tracking
   export PATH="$HOME/yandex-cloud/bin:$PATH"   # если yc установлен в домашний каталог
   yc config profile create vm-sa 2>/dev/null || true
   yc config set service-account-key key-vm-sa.json
   yc config set folder-id <FOLDER_ID>           # тот же folder_id, что в Terraform
   yc container registry configure-docker
   rm -f key-vm-sa.json                         # удалить ключ с диска после настройки
   ```
   После этого выполнить снова: `docker compose -f docker-compose.prod.yml pull`.

3. **Альтернатива (OAuth на VM).** Если по SSH заходите под пользователем с возможностью запуска браузера/логина, можно один раз выполнить на VM `yc init` (OAuth в браузере не подойдёт для headless VM) или скопировать с рабочей машины уже настроенный `~/.config/yandex-cloud/` в домашний каталог пользователя на VM (осторожно: там могут быть токены).

### Terraform: недоступен registry

Ошибка вида *The host "registry.terraform.io" ... does not offer a Terraform provider registry* или ответ с заголовком **`x-amzn-waf-reason: geo`** означает, что реестр HashiCorp недоступен (часто — геоблокировка для РФ/СНГ).

**Рекомендуемый способ — зеркало Yandex Cloud** (см. [документацию Terraform в Yandex Cloud](https://yandex.cloud/en/docs/terraform/quickstart)). Файл `~/.terraformrc` в домашнем каталоге пользователя (при наличии старого конфига: `mv ~/.terraformrc ~/.terraformrc.old`):

```hcl
provider_installation {
  network_mirror {
    url     = "https://terraform-mirror.yandexcloud.net/"
    include = ["registry.terraform.io/*/*"]
  }
  direct {
    exclude = ["registry.terraform.io/*/*"]
  }
}
```

После этого выполните в каталоге с конфигом: `terraform init`. Если используется lock-файл и провайдер не подтягивается, привяжите lock к зеркалу:

```bash
terraform providers lock -net-mirror=https://terraform-mirror.yandexcloud.net -platform=linux_amd64 -platform=windows_amd64 -platform=darwin_arm64 yandex-cloud/yandex
```

(Платформы укажите те, на которых будете запускать Terraform. Если использовали модули — сначала `terraform init`, затем удалите lock-файл и выполните `terraform providers lock`.)

**Прочие варианты:** VPN или прокси с выходом в регион, где реестр доступен; зеркало на диске (`terraform providers mirror` на другой машине + `filesystem_mirror` в `~/.terraformrc`); выполнение Terraform в CI или на сервере в другом регионе.
