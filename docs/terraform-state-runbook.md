# Terraform state: удалённое хранение и восстановление

State основной инфраструктуры (`infrastructure/yandex-cloud/`) хранится в Yandex Object Storage, а не в локальном `terraform.tfstate`.

- Бакет `experiment-tracking-tf-state`: версионирование, шифрование KMS, старые версии удаляются через 90 дней.
- Блокировки: serverless YDB (Document API, DynamoDB-совместимый), таблица `tfstate-lock`.
- Ресурсы бакета, KMS-ключа, сервисного аккаунта и YDB описаны в `infrastructure/yandex-cloud/bootstrap/` (отдельный корневой модуль с локальным state, применяется один раз).
- Ключи доступа и `backend.hcl` в git не попадают (`.gitignore`).

Нужен Terraform >= 1.6.3.

> Блокировка через DynamoDB в Terraform с 1.11 помечена устаревшей, но работает и поддерживается Yandex. Если перейдёте на более новый Terraform и появятся предупреждения, это ожидаемо.

## 1. Первичная настройка (один раз, делает владелец облака)

```bash
cd infrastructure/yandex-cloud/bootstrap
cp terraform.tfvars.example terraform.tfvars   # cloud_id, folder_id
export YC_TOKEN=$(yc iam create-token)         # либо yc_token в tfvars
terraform init
terraform apply
```

Локальный `bootstrap/terraform.tfstate` содержит секретный ключ сервисного аккаунта. Храните его вне git (например, в менеджере паролей) и никому не пересылайте. Он нужен только для изменений самого бакета.

Ключи для работы с основным state:

```bash
terraform output -raw access_key
terraform output -raw secret_key
```

### Таблица блокировок

Провайдер Terraform не умеет создавать документные таблицы YDB, поэтому таблицу создаём через AWS CLI:

```bash
export AWS_ACCESS_KEY_ID=$(terraform output -raw access_key)
export AWS_SECRET_ACCESS_KEY=$(terraform output -raw secret_key)
aws dynamodb create-table \
  --endpoint-url "$(terraform output -raw lock_document_api_endpoint)" \
  --region ru-central1 \
  --table-name tfstate-lock \
  --attribute-definitions AttributeName=LockID,AttributeType=S \
  --key-schema AttributeName=LockID,KeyType=HASH
```

## 2. Миграция существующего state

Выполняется один раз, пока у кого-то лежит локальный `terraform.tfstate`. Остальные в это время ничего не применяют.

```bash
cd infrastructure/yandex-cloud
cp backend.hcl.example backend.hcl    # подставить cloud-id и database-id в endpoint
export AWS_ACCESS_KEY_ID=...          # из bootstrap output
export AWS_SECRET_ACCESS_KEY=...
terraform init -migrate-state -backend-config=backend.hcl   # ответить yes на копирование state
terraform plan                        # должен показать No changes
```

Если план не пустой, не применяйте его: сравните с версией в бакете и локальной копией (`terraform state pull`) и разберитесь, чем они отличаются.

После успешного плана:

1. Убедитесь, что объект `terraform.tfstate` есть в бакете: `yc storage s3api list-objects --bucket experiment-tracking-tf-state`.
2. Локальные `terraform.tfstate` и `terraform.tfstate.backup` перенесите во временное безопасное место, через неделю без проблем удалите.

## 2.1. Развёртывание с нуля (локального state нет)

Если инфраструктура пересоздаётся и мигрировать нечего, шаг 2 пропускается: применяете `bootstrap/` (шаг 1, включая таблицу блокировок), настраиваете `backend.hcl` и ключи и сразу запускаете основной модуль:

```bash
cd infrastructure/yandex-cloud
terraform init -backend-config=backend.hcl
terraform apply
```

State с первого `apply` пишется в бакет. Бакет и сервисный аккаунт state живут отдельно от основной инфраструктуры, поэтому пересоздание основного модуля их не затрагивает.

## 3. Ежедневная работа

```bash
export AWS_ACCESS_KEY_ID=...
export AWS_SECRET_ACCESS_KEY=...
terraform init -backend-config=backend.hcl
terraform plan
terraform apply
```

Новому участнику нужны: доступ к репозиторию, ключи доступа от владельца облака (выдаются через менеджер паролей), `backend.hcl` по образцу `backend.hcl.example`.

Ключи лучше выдавать отдельным сервисным аккаунтом на каждого человека или CI с ролями `storage.editor` на бакет и `ydb.editor` на базу, чем делиться ключом `terraform-state`.

## 4. Восстановление

### Откат к предыдущей версии state

Бакет версионирован, каждая запись state сохраняет прежнюю версию.

```bash
aws --endpoint-url https://storage.yandexcloud.net s3api list-object-versions \
  --bucket experiment-tracking-tf-state --prefix terraform.tfstate
aws --endpoint-url https://storage.yandexcloud.net s3api get-object \
  --bucket experiment-tracking-tf-state --key terraform.tfstate \
  --version-id <VERSION_ID> restored.tfstate
terraform state push restored.tfstate    # при несовпадении serial добавьте -force, только осознанно
terraform plan
```

Файл `restored.tfstate` содержит секреты, удалите его после использования.

### Зависшая блокировка

Если `terraform apply` упал и следующий запуск пишет `Error acquiring the state lock`, убедитесь, что никто другой не выполняет apply, затем:

```bash
terraform force-unlock <LOCK_ID>
```

`LOCK_ID` указан в тексте ошибки.

### Потеряли ключи доступа

Создайте новые: `terraform apply -replace=yandex_iam_service_account_static_access_key.tfstate` в `bootstrap/` (нужен его локальный state), старые ключи перестанут работать. Если и bootstrap state потерян, создайте ключ вручную: `yc iam access-key create --service-account-name terraform-state`.

## Чего не делать

- Не коммитьте `backend.hcl`, `*.tfstate`, `*.tfvars`, ключи доступа.
- Не удаляйте бакет и KMS-ключ: `prevent_destroy` в `bootstrap/main.tf` это блокирует, `force_destroy = false`.
- Не запускайте `terraform apply` параллельно с другим человеком в обход блокировки (`-lock=false`).
