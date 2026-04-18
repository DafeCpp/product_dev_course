# ============================================
# Container Registry
# ============================================

resource "yandex_container_registry" "main" {
  name      = var.cr_name
  folder_id = var.folder_id
  labels    = var.labels
}

# Lifecycle policy: в текущей версии провайдера из зеркала Yandex ресурс
# yandex_container_registry_lifecycle_policy не поддерживается. Политику очистки
# (удаление неиспользуемых образов, лимит по тегам) можно настроить вручную в консоли:
# Container Registry → реестр → Lifecycle policies.
# Либо использовать провайдер 0.156+ и ресурс yandex_container_repository_lifecycle_policy
# (привязка к конкретному репозиторию внутри реестра).
