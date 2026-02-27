# ============================================
# Outputs — значения для CI/CD и конфигурации
# ============================================

output "vm_public_ip" {
  description = "Public IP of the application VM"
  value       = yandex_vpc_address.vm_public_ip.external_ipv4_address[0].address
}

output "vm_internal_ip" {
  description = "Internal IP of the application VM"
  value       = yandex_compute_instance.app.network_interface[0].ip_address
}

output "container_registry_id" {
  description = "Container Registry ID"
  value       = yandex_container_registry.main.id
}

output "container_registry_url" {
  description = "Container Registry base URL for docker push/pull"
  value       = "cr.yandex/${yandex_container_registry.main.id}"
}

output "pg_cluster_id" {
  description = "Managed PostgreSQL cluster ID"
  value       = yandex_mdb_postgresql_cluster.main.id
}

output "pg_cluster_host" {
  description = "PostgreSQL cluster FQDN (use for DATABASE_URL)"
  value       = yandex_mdb_postgresql_cluster.main.host[0].fqdn
}

output "auth_database_url" {
  description = "AUTH_DATABASE_URL for auth-service"
  value       = "postgresql://auth_user:***@${yandex_mdb_postgresql_cluster.main.host[0].fqdn}:6432/auth_db?sslmode=verify-full"
  sensitive   = true
}

output "experiment_database_url" {
  description = "EXPERIMENT_DATABASE_URL for experiment-service"
  value       = "postgresql://experiment_user:***@${yandex_mdb_postgresql_cluster.main.host[0].fqdn}:6432/experiment_db?sslmode=verify-full"
  sensitive   = true
}

output "ci_sa_key_id" {
  description = "CI service account key ID (for GitHub Secrets)"
  value       = yandex_iam_service_account_key.ci_sa_key.id
  sensitive   = true
}

output "ci_sa_key_private" {
  description = "CI service account private key (PEM format, not for docker login json_key)"
  value       = yandex_iam_service_account_key.ci_sa_key.private_key
  sensitive   = true
}

# Полный JSON ключа в формате, который ожидает Container Registry (логин json_key).
# Используйте этот output для секрета YC_SA_JSON_KEY в GitHub Actions.
output "ci_sa_key_json" {
  description = "CI SA key as full JSON for Container Registry (username json_key). Use for GitHub secret YC_SA_JSON_KEY."
  value = jsonencode({
    id                 = yandex_iam_service_account_key.ci_sa_key.id
    service_account_id = yandex_iam_service_account_key.ci_sa_key.service_account_id
    created_at         = yandex_iam_service_account_key.ci_sa_key.created_at
    private_key        = yandex_iam_service_account_key.ci_sa_key.private_key
  })
  sensitive = true
}

output "ssh_connect_command" {
  description = "SSH command to connect to the VM"
  value       = "ssh ${var.vm_user}@${yandex_vpc_address.vm_public_ip.external_ipv4_address[0].address}"
}

output "app_url" {
  description = "Application URL"
  value       = var.domain_name != "" ? "https://${var.domain_name}" : "http://${yandex_vpc_address.vm_public_ip.external_ipv4_address[0].address}"
}
