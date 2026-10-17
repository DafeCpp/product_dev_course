variable "yc_token" {
  description = "Yandex Cloud OAuth/IAM token. Можно оставить пустым и задать YC_TOKEN в окружении."
  type        = string
  sensitive   = true
  default     = ""
}

variable "cloud_id" {
  description = "Yandex Cloud ID"
  type        = string
}

variable "folder_id" {
  description = "Yandex Cloud Folder ID"
  type        = string
}

variable "zone" {
  description = "Yandex Cloud availability zone"
  type        = string
  default     = "ru-central1-a"
}

variable "state_bucket_name" {
  description = "Глобально уникальное имя бакета для Terraform state"
  type        = string
  default     = "experiment-tracking-tf-state"

  validation {
    condition     = length(var.state_bucket_name) >= 3 && length(var.state_bucket_name) <= 63
    error_message = "state_bucket_name must contain between 3 and 63 characters."
  }
}

variable "noncurrent_version_retention_days" {
  description = "Сколько дней хранить предыдущие версии state для отката"
  type        = number
  default     = 90

  validation {
    condition     = var.noncurrent_version_retention_days >= 7
    error_message = "Keep old state versions for at least 7 days."
  }
}
