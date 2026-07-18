# ============================================
# Object Storage — experiment-service artifacts
# ============================================

resource "yandex_iam_service_account" "artifacts_sa" {
  name        = "experiment-artifacts"
  description = "Bucket-scoped service account for experiment-service artifacts"
}

resource "yandex_iam_service_account_static_access_key" "artifacts_sa_key" {
  service_account_id = yandex_iam_service_account.artifacts_sa.id
  description        = "S3 key for experiment-service artifact storage"
}

resource "yandex_storage_bucket" "artifacts" {
  bucket    = var.artifacts_bucket_name
  folder_id = var.folder_id
  # Buckets are private by default; public ACL/policy is intentionally absent.
  force_destroy = false

  cors_rule {
    allowed_headers = ["*"]
    allowed_methods = ["GET", "HEAD", "PUT"]
    allowed_origins = var.artifacts_cors_allowed_origins
    expose_headers  = ["ETag"]
    max_age_seconds = 3600
  }
}

# Deliberately bucket-scoped: the runtime identity cannot access other buckets
# in the folder. storage.editor is required for upload, download and delete.
resource "yandex_storage_bucket_iam_binding" "artifacts_editor" {
  bucket = yandex_storage_bucket.artifacts.bucket
  role   = "storage.editor"
  members = [
    "serviceAccount:${yandex_iam_service_account.artifacts_sa.id}",
  ]
}
