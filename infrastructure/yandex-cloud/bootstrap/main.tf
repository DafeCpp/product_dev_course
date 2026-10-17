# ============================================
# Bootstrap: remote state для infrastructure/yandex-cloud
# ============================================
#
# Отдельный Terraform-корень, который создаёт всё, что нужно основному корню
# для хранения state в Yandex Object Storage:
#   - бакет со state (версионирование, шифрование KMS, без публичного доступа);
#   - сервисный аккаунт с правами только на этот бакет и на базу блокировок;
#   - serverless YDB для блокировок (Document API, совместим с DynamoDB-протоколом).
#
# State самого bootstrap хранится локально: это один маленький корень, который
# применяется один раз. Порядок работы и восстановление — в
# docs/terraform-state-runbook.md.

terraform {
  required_version = ">= 1.6.3"

  required_providers {
    yandex = {
      source  = "yandex-cloud/yandex"
      version = ">= 0.100.0"
    }
  }
}

provider "yandex" {
  token     = var.yc_token
  cloud_id  = var.cloud_id
  folder_id = var.folder_id
  zone      = var.zone
}

# Ключ шифрования содержимого бакета: state содержит пароли БД и JWT-секрет.
resource "yandex_kms_symmetric_key" "tfstate" {
  name              = "${var.state_bucket_name}-key"
  description       = "Encrypts Terraform state bucket"
  default_algorithm = "AES_256"
  rotation_period   = "8760h"

  lifecycle {
    prevent_destroy = true
  }
}

resource "yandex_iam_service_account" "tfstate" {
  name        = "terraform-state"
  description = "Access to the Terraform state bucket and lock database"
}

resource "yandex_iam_service_account_static_access_key" "tfstate" {
  service_account_id = yandex_iam_service_account.tfstate.id
  description        = "S3 key for the Terraform s3 backend"
}

# Нужен, чтобы бакет мог шифровать/расшифровывать объекты этим ключом.
resource "yandex_kms_symmetric_key_iam_binding" "tfstate_encrypter_decrypter" {
  symmetric_key_id = yandex_kms_symmetric_key.tfstate.id
  role             = "kms.keys.encrypterDecrypter"
  members = [
    "serviceAccount:${yandex_iam_service_account.tfstate.id}",
  ]
}

resource "yandex_storage_bucket" "tfstate" {
  bucket    = var.state_bucket_name
  folder_id = var.folder_id

  # Бакет со state нельзя удалить вместе с содержимым.
  force_destroy = false

  # Приватный: публичные ACL и политики намеренно не задаются.

  versioning {
    enabled = true
  }

  server_side_encryption_configuration {
    rule {
      apply_server_side_encryption_by_default {
        kms_master_key_id = yandex_kms_symmetric_key.tfstate.id
        sse_algorithm     = "aws:kms"
      }
    }
  }

  # Старые версии state нужны для отката, но не вечно.
  lifecycle_rule {
    id      = "expire-old-state-versions"
    enabled = true

    noncurrent_version_expiration {
      days = var.noncurrent_version_retention_days
    }
  }

  lifecycle {
    prevent_destroy = true
  }
}

# Доступ только к этому бакету, а не ко всему каталогу.
resource "yandex_storage_bucket_iam_binding" "tfstate_editor" {
  bucket = yandex_storage_bucket.tfstate.bucket
  role   = "storage.editor"
  members = [
    "serviceAccount:${yandex_iam_service_account.tfstate.id}",
  ]
}

# База для блокировок state (таблица создаётся командой из runbook:
# Terraform-провайдер не умеет document-таблицы).
resource "yandex_ydb_database_serverless" "tfstate_lock" {
  name      = "${var.state_bucket_name}-lock"
  folder_id = var.folder_id

  deletion_protection = true

  serverless_database {
    storage_size_limit = 1
  }
}

resource "yandex_ydb_database_iam_binding" "tfstate_lock_editor" {
  database_id = yandex_ydb_database_serverless.tfstate_lock.id
  role        = "ydb.editor"
  members = [
    "serviceAccount:${yandex_iam_service_account.tfstate.id}",
  ]
}
