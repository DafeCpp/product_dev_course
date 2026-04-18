# ============================================
# Managed PostgreSQL Cluster
# ============================================
#
# Один кластер с двумя базами данных:
#   - auth_db       (пользователь auth_user)
#   - experiment_db (пользователь experiment_user) + расширение timescaledb

resource "yandex_mdb_postgresql_cluster" "main" {
  name        = var.pg_cluster_name
  environment = "PRODUCTION"
  network_id  = yandex_vpc_network.main.id
  labels      = var.labels

  config {
    version = var.pg_version

    resources {
      resource_preset_id = var.pg_resource_preset
      disk_type_id       = var.pg_disk_type
      disk_size          = var.pg_disk_size_gb
    }

    postgresql_config = {
      shared_preload_libraries = "SHARED_PRELOAD_LIBRARIES_TIMESCALEDB"
    }
  }

  host {
    zone             = var.zone
    subnet_id        = yandex_vpc_subnet.main.id
    assign_public_ip = false
  }

  security_group_ids = [yandex_vpc_security_group.pg.id]
}

# --- Users ---
# Имя "postgres" в Yandex Managed PostgreSQL зарезервировано; используем var.pg_admin_username (по умолчанию cluster_admin).

resource "yandex_mdb_postgresql_user" "admin" {
  cluster_id = yandex_mdb_postgresql_cluster.main.id
  name       = var.pg_admin_username
  password   = var.pg_admin_password
}

resource "yandex_mdb_postgresql_user" "auth_user" {
  cluster_id = yandex_mdb_postgresql_cluster.main.id
  name       = "auth_user"
  password   = var.pg_auth_db_password
  grants     = []
}

resource "yandex_mdb_postgresql_user" "experiment_user" {
  cluster_id = yandex_mdb_postgresql_cluster.main.id
  name       = "experiment_user"
  password   = var.pg_experiment_db_password
  grants     = []
}

# --- Databases ---

resource "yandex_mdb_postgresql_database" "auth_db" {
  cluster_id = yandex_mdb_postgresql_cluster.main.id
  name       = "auth_db"
  owner      = yandex_mdb_postgresql_user.auth_user.name

  extension {
    name = "pgcrypto"
  }

  depends_on = [yandex_mdb_postgresql_user.auth_user]
}

resource "yandex_mdb_postgresql_database" "experiment_db" {
  cluster_id = yandex_mdb_postgresql_cluster.main.id
  name       = "experiment_db"
  owner      = yandex_mdb_postgresql_user.experiment_user.name

  extension {
    name = "timescaledb"
  }
  extension {
    name = "pgcrypto"
  }

  depends_on = [yandex_mdb_postgresql_user.experiment_user]
}
