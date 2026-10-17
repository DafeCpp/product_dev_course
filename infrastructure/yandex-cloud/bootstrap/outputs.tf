output "state_bucket" {
  description = "bucket для backend.hcl"
  value       = yandex_storage_bucket.tfstate.bucket
}

output "lock_document_api_endpoint" {
  description = "dynamodb endpoint для backend.hcl и для создания таблицы блокировок"
  value       = yandex_ydb_database_serverless.tfstate_lock.document_api_endpoint
}

output "lock_database_path" {
  description = "Путь базы YDB (нужен для справки)"
  value       = yandex_ydb_database_serverless.tfstate_lock.database_path
}

output "access_key" {
  description = "AWS_ACCESS_KEY_ID для backend (не коммитить)"
  value       = yandex_iam_service_account_static_access_key.tfstate.access_key
  sensitive   = true
}

output "secret_key" {
  description = "AWS_SECRET_ACCESS_KEY для backend; доступен только при создании ключа (не коммитить)"
  value       = yandex_iam_service_account_static_access_key.tfstate.secret_key
  sensitive   = true
}
