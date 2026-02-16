# ============================================
# Container Registry
# ============================================

resource "yandex_container_registry" "main" {
  name      = var.cr_name
  folder_id = var.folder_id
  labels    = var.labels
}

# Lifecycle policy — удаляем неиспользуемые образы старше 7 дней, храним не более 10
resource "yandex_container_registry_lifecycle_policy" "cleanup" {
  name        = "cleanup-old-images"
  status      = "active"
  registry_id = yandex_container_registry.main.id

  rule {
    description   = "Remove untagged images older than 7 days"
    untagged      = true
    expire_period = "168h"
  }

  rule {
    description  = "Keep only last 10 tagged images per repo"
    tag_regexp   = ".*"
    retained_top = 10
  }
}
