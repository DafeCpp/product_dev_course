# ============================================
# Yandex Cloud Infrastructure — Experiment Tracking Platform
# ============================================
#
# Использование:
#   1. Установить Yandex Cloud CLI и Terraform
#   2. Заполнить terraform.tfvars (скопировать из terraform.tfvars.example)
#   3. terraform init
#   4. terraform plan
#   5. terraform apply
#
# Создаёт:
#   - VPC + подсеть
#   - Managed PostgreSQL (с TimescaleDB)
#   - Container Registry
#   - Compute Instance (VM для Docker Compose)
#   - Static IP + Security Groups
#   - Service Account + IAM bindings

terraform {
  required_version = ">= 1.5.0"

  required_providers {
    yandex = {
      source  = "yandex-cloud/yandex"
      version = ">= 0.100.0"
    }
  }

  # После первого деплоя рекомендуется перенести state в Yandex Object Storage:
  # backend "s3" {
  #   endpoints = { s3 = "https://storage.yandexcloud.net" }
  #   bucket    = "experiment-tracking-tf-state"
  #   region    = "ru-central1"
  #   key       = "terraform.tfstate"
  #
  #   skip_region_validation      = true
  #   skip_credentials_validation = true
  #   skip_requesting_account_id  = true
  #   skip_s3_checksum            = true
  # }
}

provider "yandex" {
  cloud_id  = var.cloud_id
  folder_id = var.folder_id
  zone      = var.zone
}
