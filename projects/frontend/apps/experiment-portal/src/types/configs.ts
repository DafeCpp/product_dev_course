export type ConfigType = 'feature_flag' | 'qos'

export interface ConfigResponse {
  id: string
  service_name: string
  project_id: string | null
  key: string
  config_type: ConfigType
  description: string | null
  value: unknown
  metadata: Record<string, unknown>
  is_active: boolean
  is_critical: boolean
  is_sensitive: boolean
  version: number
  created_by: string
  updated_by: string
  created_at: string
  updated_at: string
}

export interface ConfigsListResponse {
  items: ConfigResponse[]
  next_cursor: string | null
}

export interface ConfigCreate {
  service_name: string
  key: string
  config_type: ConfigType
  description?: string
  value: unknown
  metadata?: Record<string, unknown>
  is_critical?: boolean
  is_sensitive?: boolean
}

export interface ConfigPatch {
  version: number
  value?: unknown
  description?: string
  metadata?: Record<string, unknown>
  is_critical?: boolean
  is_sensitive?: boolean
  change_reason?: string
}

export interface DryRunResponse {
  preview: unknown
  dry_run: true
}

export interface ConfigHistoryItem {
  id: string
  config_id: string
  version: number
  service_name: string
  key: string
  config_type: ConfigType
  value: unknown
  metadata: Record<string, unknown>
  is_active: boolean
  changed_by: string
  change_reason: string | null
  correlation_id: string | null
  changed_at: string
}

export interface ConfigHistoryResponse {
  items: ConfigHistoryItem[]
}

export interface ConfigSchema {
  id: string
  config_type: ConfigType
  schema: Record<string, unknown>
  version: number
  is_active: boolean
  created_by: string
  created_at: string
}

export interface SchemasListResponse {
  items: ConfigSchema[]
}

/** A successful write returns the config plus the server-provided version (ETag). */
export interface ConfigWriteResult {
  config: ConfigResponse
  /** Numeric version parsed from the ETag response header. */
  etag: number
}
