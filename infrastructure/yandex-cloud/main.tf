# ============================================
# Yandex Cloud Infrastructure — Experiment Tracking Platform
# ============================================
#
# Использование:
#   1. Установить Yandex Cloud CLI и Terraform
#   2. (При недоступности registry.terraform.io) Настроить ~/.terraformrc с network_mirror
#      https://terraform-mirror.yandexcloud.net/ — см. docs/deployment-yandex-cloud.md
#   3. Заполнить terraform.tfvars (скопировать из terraform.tfvars.example)
#   4. Один раз применить bootstrap/ (бакет для state) и настроить backend.hcl
#      и ключи доступа — docs/terraform-state-runbook.md
#   5. terraform init -backend-config=backend.hcl
#   6. terraform plan && terraform apply
#
# Создаёт:
#   - VPC + подсеть
#   - Managed PostgreSQL (с TimescaleDB)
#   - Container Registry
#   - Compute Instance (VM для Docker Compose)
#   - Static IP + Security Groups
#   - Service Account + IAM bindings

terraform {
  required_version = ">= 1.6.3"

  required_providers {
    yandex = {
      source  = "yandex-cloud/yandex"
      version = ">= 0.100.0"
    }
  }

  # State лежит в Yandex Object Storage (бакет и таблица блокировок создаются
  # в bootstrap/). Значения, зависящие от окружения, и ключи доступа сюда не
  # попадают: bucket/key/dynamodb_* берутся из backend.hcl, ключи из
  # AWS_ACCESS_KEY_ID / AWS_SECRET_ACCESS_KEY. См. docs/terraform-state-runbook.md.
  backend "s3" {
    endpoints = { s3 = "https://storage.yandexcloud.net" }
    region    = "ru-central1"

    skip_region_validation      = true
    skip_credentials_validation = true
    skip_requesting_account_id  = true
    skip_s3_checksum            = true
  }
}

provider "yandex" {
  token     = var.yc_token
  cloud_id  = var.cloud_id
  folder_id = var.folder_id
  zone      = var.zone
}
