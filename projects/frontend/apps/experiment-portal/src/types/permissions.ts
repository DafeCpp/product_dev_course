export interface Permission {
  id: string
  name: string
  description: string
  category: string
  scope: 'system' | 'project'
}

export interface Role {
  id: string
  name: string
  description: string | null
  scope: 'system' | 'project'
  is_builtin: boolean
  project_id: string | null
  permissions: Permission[]
  created_at: string
  updated_at: string
}

export interface EffectivePermissions {
  system_permissions: string[]
  project_permissions: string[]
  is_superadmin: boolean
}

export interface GrantRoleRequest {
  role_id: string
  expires_at?: string
}

export interface AuditEntry {
  id: string
  actor_id: string
  actor_username: string
  action: string
  scope_type: string | null
  scope_id: string | null
  target_type: string | null
  target_id: string | null
  details: Record<string, unknown>
  ip_address: string | null
  created_at: string
}
