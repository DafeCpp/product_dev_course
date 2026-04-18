# ============================================
# Service Accounts & IAM
# ============================================

# SA для VM — нужен для pull образов из Container Registry
resource "yandex_iam_service_account" "vm_sa" {
  name        = "${var.vm_name}-sa"
  description = "Service account for the application VM"
}

# Разрешаем VM пуллить образы из Container Registry (требуются права на управление IAM каталога)
resource "yandex_resourcemanager_folder_iam_member" "vm_cr_puller" {
  count      = var.manage_folder_iam ? 1 : 0
  folder_id  = var.folder_id
  role       = "container-registry.images.puller"
  member     = "serviceAccount:${yandex_iam_service_account.vm_sa.id}"
}

# SA для CI/CD (GitHub Actions) — пушит образы + деплоит
resource "yandex_iam_service_account" "ci_sa" {
  name        = "experiment-tracking-ci"
  description = "Service account for CI/CD (GitHub Actions)"
}

resource "yandex_resourcemanager_folder_iam_member" "ci_cr_pusher" {
  count      = var.manage_folder_iam ? 1 : 0
  folder_id  = var.folder_id
  role       = "container-registry.images.pusher"
  member     = "serviceAccount:${yandex_iam_service_account.ci_sa.id}"
}

resource "yandex_resourcemanager_folder_iam_member" "ci_cr_puller" {
  count      = var.manage_folder_iam ? 1 : 0
  folder_id  = var.folder_id
  role       = "container-registry.images.puller"
  member     = "serviceAccount:${yandex_iam_service_account.ci_sa.id}"
}

# Авторизованный ключ для CI/CD SA (используется в GitHub Actions)
resource "yandex_iam_service_account_key" "ci_sa_key" {
  service_account_id = yandex_iam_service_account.ci_sa.id
  description        = "Key for GitHub Actions CI/CD"
}
