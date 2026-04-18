# ============================================
# Сеть, подсеть, Security Groups
# ============================================

resource "yandex_vpc_network" "main" {
  name   = var.vpc_name
  labels = var.labels
}

resource "yandex_vpc_subnet" "main" {
  name           = "${var.vpc_name}-subnet-a"
  zone           = var.zone
  network_id     = yandex_vpc_network.main.id
  v4_cidr_blocks = [var.subnet_cidr]
  labels         = var.labels
}

# --- Static public IP for the VM ---

resource "yandex_vpc_address" "vm_public_ip" {
  name = "${var.vm_name}-public-ip"

  external_ipv4_address {
    zone_id = var.zone
  }

  labels = var.labels
}

# --- Security Group ---

resource "yandex_vpc_security_group" "app" {
  name       = "${var.vpc_name}-app-sg"
  network_id = yandex_vpc_network.main.id
  labels     = var.labels

  # SSH
  ingress {
    protocol       = "TCP"
    port           = 22
    v4_cidr_blocks = ["0.0.0.0/0"]
    description    = "SSH"
  }

  # HTTP (Experiment Portal)
  ingress {
    protocol       = "TCP"
    port           = 80
    v4_cidr_blocks = ["0.0.0.0/0"]
    description    = "HTTP — Experiment Portal"
  }

  # HTTPS
  ingress {
    protocol       = "TCP"
    port           = 443
    v4_cidr_blocks = ["0.0.0.0/0"]
    description    = "HTTPS"
  }

  # Auth Proxy (BFF)
  ingress {
    protocol       = "TCP"
    port           = 8080
    v4_cidr_blocks = ["0.0.0.0/0"]
    description    = "Auth Proxy BFF"
  }

  # Sensor Simulator
  ingress {
    protocol       = "TCP"
    port           = 8082
    v4_cidr_blocks = ["0.0.0.0/0"]
    description    = "Sensor Simulator"
  }

  # Telemetry Ingest (public API for devices)
  ingress {
    protocol       = "TCP"
    port           = 8003
    v4_cidr_blocks = ["0.0.0.0/0"]
    description    = "Telemetry Ingest API (devices)"
  }

  # Grafana
  ingress {
    protocol       = "TCP"
    port           = 3001
    v4_cidr_blocks = ["0.0.0.0/0"]
    description    = "Grafana UI"
  }

  # All outgoing traffic
  egress {
    protocol       = "ANY"
    v4_cidr_blocks = ["0.0.0.0/0"]
    description    = "Allow all outbound"
  }
}

# Security group for Managed PostgreSQL — allows only from VM subnet
resource "yandex_vpc_security_group" "pg" {
  name       = "${var.vpc_name}-pg-sg"
  network_id = yandex_vpc_network.main.id
  labels     = var.labels

  ingress {
    protocol       = "TCP"
    port           = 6432
    v4_cidr_blocks = [var.subnet_cidr]
    description    = "PostgreSQL from app subnet"
  }

  egress {
    protocol       = "ANY"
    v4_cidr_blocks = ["0.0.0.0/0"]
    description    = "Allow all outbound"
  }
}
