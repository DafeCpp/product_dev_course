# ============================================
# Compute Instance (VM) — runs Docker Compose
# ============================================

# Используем Container Optimized Image (COI) от Yandex Cloud —
# Ubuntu с предустановленным Docker и Docker Compose

data "yandex_compute_image" "coi" {
  family    = "container-optimized-image"
  folder_id = "standard-images"
}

resource "yandex_compute_instance" "app" {
  name        = var.vm_name
  platform_id = var.vm_platform_id
  zone        = var.zone
  labels      = var.labels

  resources {
    cores         = var.vm_cores
    memory        = var.vm_memory_gb
    core_fraction = var.vm_core_fraction
  }

  scheduling_policy {
    preemptible = var.vm_preemptible
  }

  boot_disk {
    initialize_params {
      image_id = var.vm_image_id != "" ? var.vm_image_id : data.yandex_compute_image.coi.id
      size     = var.vm_disk_size_gb
      type     = "network-ssd"
    }
  }

  network_interface {
    subnet_id          = yandex_vpc_subnet.main.id
    nat                = true
    nat_ip_address     = yandex_vpc_address.vm_public_ip.external_ipv4_address[0].address
    security_group_ids = [yandex_vpc_security_group.app.id]
  }

  metadata = {
    user-data = templatefile("${path.module}/cloud-init.yaml", {
      ssh_user       = var.vm_user
      ssh_public_key = file(var.vm_ssh_public_key_path)
      registry_id    = yandex_container_registry.main.id
    })
  }

  service_account_id = yandex_iam_service_account.vm_sa.id

  allow_stopping_for_update = true
}
