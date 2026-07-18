# ============================================
# Входные переменные
# ============================================

# --- Yandex Cloud credentials ---

variable "yc_token" {
  description = "Yandex Cloud OAuth token"
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

variable "manage_folder_iam" {
  description = "Create folder IAM bindings for VM and CI service accounts. Set to false if Terraform identity has no permission to manage folder IAM (then grant container-registry.images.puller/pusher to SAs manually)."
  type        = bool
  default     = true
}

variable "zone" {
  description = "Yandex Cloud availability zone"
  type        = string
  default     = "ru-central1-a"
}

# --- Networking ---

variable "vpc_name" {
  description = "Name of the VPC network"
  type        = string
  default     = "experiment-tracking-vpc"
}

variable "subnet_cidr" {
  description = "CIDR block for the subnet"
  type        = string
  default     = "10.128.0.0/24"
}

# --- Compute Instance (VM) ---

variable "vm_name" {
  description = "Name of the compute instance"
  type        = string
  default     = "experiment-tracking-vm"
}

variable "vm_platform_id" {
  description = "Yandex Cloud platform (cpu type)"
  type        = string
  default     = "standard-v3"
}

variable "vm_cores" {
  description = "Number of CPU cores"
  type        = number
  default     = 2
}

variable "vm_memory_gb" {
  description = "RAM in GB"
  type        = number
  default     = 6
}

variable "vm_core_fraction" {
  description = "Guaranteed vCPU share (%). 20/50/100. Use 50 for dev, 100 for prod."
  type        = number
  default     = 50
}

variable "vm_disk_size_gb" {
  description = "Boot disk size in GB (Docker images + logs)"
  type        = number
  default     = 90
}

variable "vm_image_id" {
  description = "Boot disk image ID (Container Optimized Image)"
  type        = string
  default     = ""
}

variable "vm_user" {
  description = "SSH user for the VM"
  type        = string
  default     = "deploy"
}

variable "vm_ssh_public_key_path" {
  description = "Path to SSH public key for VM access"
  type        = string
  default     = "~/.ssh/id_ed25519.pub"
}

variable "vm_preemptible" {
  description = "Use a preemptible VM. Must remain false for production."
  type        = bool
  default     = false
}

# --- Managed PostgreSQL ---

variable "pg_cluster_name" {
  description = "Name of the Managed PostgreSQL cluster"
  type        = string
  default     = "experiment-tracking-pg"
}

variable "pg_version" {
  description = "PostgreSQL version"
  type        = string
  default     = "16"
}

variable "pg_resource_preset" {
  description = "Resource preset for PG host (s2.micro = 2 vCPU, 8 GB)"
  type        = string
  default     = "s2.micro"
}

variable "pg_disk_size_gb" {
  description = "Disk size for PG cluster in GB"
  type        = number
  default     = 20
}

variable "pg_disk_type" {
  description = "Disk type for PG cluster"
  type        = string
  default     = "network-ssd"
}

variable "pg_admin_username" {
  description = "PostgreSQL admin username. Do not use 'postgres' — it is reserved in Yandex Managed PostgreSQL."
  type        = string
  default     = "cluster_admin"
}

variable "pg_admin_password" {
  description = "PostgreSQL admin user password"
  type        = string
  sensitive   = true
}

variable "pg_auth_db_password" {
  description = "Password for auth_user (auth_db)"
  type        = string
  sensitive   = true
}

variable "pg_experiment_db_password" {
  description = "Password for experiment_user (experiment_db)"
  type        = string
  sensitive   = true
}

variable "pg_config_db_password" {
  description = "Password for config_user (config_db)"
  type        = string
  sensitive   = true
}

variable "pg_script_db_password" {
  description = "Password for script_user (script_db)"
  type        = string
  sensitive   = true
}

# --- Container Registry ---

variable "cr_name" {
  description = "Name of the Container Registry"
  type        = string
  default     = "experiment-tracking-cr"
}

# --- Object Storage ---

variable "artifacts_bucket_name" {
  description = "Globally unique private Object Storage bucket for experiment-service artifacts"
  type        = string

  validation {
    condition     = length(var.artifacts_bucket_name) >= 3 && length(var.artifacts_bucket_name) <= 63
    error_message = "artifacts_bucket_name must contain between 3 and 63 characters."
  }
}

variable "artifacts_cors_allowed_origins" {
  description = "Browser origins allowed to upload/download artifacts through presigned URLs"
  type        = list(string)

  validation {
    condition     = length(var.artifacts_cors_allowed_origins) > 0 && alltrue([for origin in var.artifacts_cors_allowed_origins : startswith(origin, "https://")])
    error_message = "artifacts_cors_allowed_origins must contain at least one HTTPS origin."
  }
}

# --- Application ---

variable "jwt_secret" {
  description = "JWT secret key for auth-service"
  type        = string
  sensitive   = true
}

variable "domain_name" {
  description = "Domain name for the application (optional, for TLS)"
  type        = string
  default     = ""
}

# --- Tags ---

variable "labels" {
  description = "Labels to apply to all resources"
  type        = map(string)
  default = {
    project     = "experiment-tracking"
    environment = "production"
    managed-by  = "terraform"
  }
}
