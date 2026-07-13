# Инициализация первого администратора

## Обзор

При развёртывании Experiment Tracking Platform необходимо создать первого администратора для управления системой. Это может быть сделано автоматически через seed-скрипт во время инициализации БД.

## Development (локальная разработка)

### Через make-команду

```bash
make dev-up        # Запуск docker-compose
make auth-init     # Миграции + создание админа
```

По умолчанию, если `ADMIN_PASSWORD` не установлена, скрипт пропускается (идемпотентно).

### С кастомным паролем

```bash
ADMIN_PASSWORD=MySecurePassword123 make auth-init
```

### Через bootstrap API (альтернатива)

```bash
make dev-seed
# или
curl -X POST http://localhost:8001/auth/admin/bootstrap \
  -H 'Content-Type: application/json' \
  -d '{
    "bootstrap_secret": "dev-bootstrap-secret",
    "username": "admin",
    "email": "admin@example.com",
    "password": "Admin123"
  }'
```

## Production (Yandex Cloud)

Production bootstrap-данные не являются Terraform-переменными. Не добавляйте
`admin_username`, `admin_email` или `admin_password` в
`infrastructure/yandex-cloud/terraform.tfvars`: Terraform их не использует, а
state и сохранённый plan могут раскрыть переданные секреты.

Миграции auth-service выполняет one-shot сервис `auth-migrate` при deploy.
`make auth-init` предназначен только для development: он ожидает локальный
PostgreSQL из `docker-compose.yml`, которого нет в production compose.

После успешного deploy создайте первого администратора одноразовым запуском
seed внутри работающего `auth-service`:

```bash
ssh deploy@<VM_IP>
cd /opt/experiment-tracking

read -rp "Admin username [admin]: " ADMIN_USERNAME
ADMIN_USERNAME="${ADMIN_USERNAME:-admin}"
read -rp "Admin email: " ADMIN_EMAIL
read -rsp "Admin password: " ADMIN_PASSWORD
echo

docker compose -f docker-compose.prod.yml exec -T \
  -e ADMIN_USERNAME="$ADMIN_USERNAME" \
  -e ADMIN_EMAIL="$ADMIN_EMAIL" \
  -e ADMIN_PASSWORD="$ADMIN_PASSWORD" \
  auth-service sh -c '
    python -m bin.seed \
      --database-url "$DATABASE_URL" \
      --username "$ADMIN_USERNAME" \
      --email "$ADMIN_EMAIL" \
      --password "$ADMIN_PASSWORD"
  '

unset ADMIN_PASSWORD ADMIN_EMAIL ADMIN_USERNAME
```

Seed идемпотентен: если активный admin или superadmin уже существует, повторный
запуск ничего не изменит.

### Безопасность production bootstrap

- Генерируйте уникальный пароль длиной не менее 8 символов, например через
  password manager или `openssl rand -base64 32`.
- Не сохраняйте bootstrap-пароль в Git, Terraform, `.env` или shell history.
- После команды удалите пароль из окружения через `unset`.
- Дополнительных администраторов создавайте через API или управляющий интерфейс.
- Первый администратор может сменить пароль через API `/auth/me`.

## API создания админа вручную

Если нужно создать админа вручную в уже работающей системе:

```bash
# 1. Создать пользователя через API (регистрация)
curl -X POST http://localhost:8001/auth/register \
  -H 'Content-Type: application/json' \
  -d '{
    "username": "newadmin",
    "email": "newadmin@example.com",
    "password": "SecurePassword123"
  }'

# 2. Присвоить роль администратора (требуется существующий админ)
# используйте GraphQL API или SQL:
# INSERT INTO user_system_roles (user_id, role_id, granted_by)
# VALUES ('<user_id>', '00000000-0000-0000-0000-000000000002', '<admin_user_id>')
```

## Идемпотентность

Seed-скрипт идемпотентен:
- Если админ уже существует → пропускается
- Проверяет наличие пользователей с ролями `admin` или `superadmin`
- Можно безопасно запускать несколько раз

## Восстановление пароля администратора

Если пароль админа потерян (development):

```bash
make dev-reset-admin
# сбросит пароль на значение из $DEV_ADMIN_PASSWORD (по умолчанию: Admin123)
```

Для production требуется доступ к БД:

```sql
-- На VM
docker compose -f docker-compose.prod.yml exec postgres psql -U postgres -d auth_db

-- В psql:
UPDATE users SET password_change_required = true WHERE username = 'admin';
-- После этого админ может сбросить пароль через API /auth/password-reset
```

## Переменные окружения

| Переменная | Default | Описание |
|------------|---------|---------|
| `ADMIN_USERNAME` | `admin` | Одноразовый параметр `bin.seed`; не Terraform/runtime-конфигурация |
| `ADMIN_EMAIL` | `admin@example.com` | Одноразовый параметр `bin.seed`; не Terraform/runtime-конфигурация |
| `ADMIN_PASSWORD` | (не установлена) | Одноразовый секрет `bin.seed`; не хранить в Terraform или `.env` |
| `AUTH_DATABASE_URL` | `postgresql://auth_user:auth_password@postgres:5432/auth_db` | Connection string auth БД |

## Статус: как проверить

```bash
# Development
curl http://localhost:8001/auth/me -H "Authorization: Bearer $TOKEN"

# Production  
ssh deploy@<VM_IP>
docker compose -f docker-compose.prod.yml logs auth-service | grep -i "admin\|seed\|created"

# Через БД
docker compose -f docker-compose.prod.yml exec postgres psql -U postgres -d auth_db -c \
  "SELECT u.username, u.email, r.name FROM users u 
   JOIN user_system_roles usr ON u.id = usr.user_id 
   JOIN roles r ON usr.role_id = r.id WHERE r.name IN ('admin', 'superadmin');"
```
