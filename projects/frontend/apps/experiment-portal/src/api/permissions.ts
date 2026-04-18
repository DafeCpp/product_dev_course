import type {
  Permission,
  Role,
  EffectivePermissions,
  UserSystemRole,
  UserProjectRole,
} from '../types/permissions'
import { apiGet, apiPost, apiPatch, apiDelete } from './client'

export const permissionsApi = {
  // Permission catalog
  listPermissions: async (): Promise<Permission[]> => {
    return apiGet<Permission[]>('/api/v1/permissions')
  },

  // Effective permissions for a user
  getEffectivePermissions: async (
    userId: string,
    projectId?: string
  ): Promise<EffectivePermissions> => {
    const params = projectId ? { project_id: projectId } : {}
    return apiGet<EffectivePermissions>(
      `/api/v1/users/${userId}/effective-permissions`,
      { params }
    )
  },

  // System roles
  listSystemRoles: async (): Promise<Role[]> => {
    return apiGet<Role[]>('/api/v1/system-roles')
  },

  createSystemRole: async (data: {
    name: string
    description?: string
    permissions: string[]
  }): Promise<Role> => {
    return apiPost<Role>('/api/v1/system-roles', data)
  },

  updateSystemRole: async (
    roleId: string,
    data: { name?: string; description?: string; permissions?: string[] }
  ): Promise<Role> => {
    return apiPatch<Role>(`/api/v1/system-roles/${roleId}`, data)
  },

  deleteSystemRole: async (roleId: string): Promise<void> => {
    await apiDelete(`/api/v1/system-roles/${roleId}`)
  },

  // System role assignments
  grantSystemRole: async (
    userId: string,
    roleIdOrBody: string | { role_id: string; expires_at?: string },
    expiresAt?: string
  ): Promise<UserSystemRole> => {
    const body =
      typeof roleIdOrBody === 'string'
        ? { role_id: roleIdOrBody, ...(expiresAt ? { expires_at: expiresAt } : {}) }
        : roleIdOrBody
    return apiPost<UserSystemRole>(`/api/v1/users/${userId}/system-roles`, body)
  },

  revokeSystemRole: async (userId: string, roleId: string): Promise<void> => {
    await apiDelete(`/api/v1/users/${userId}/system-roles/${roleId}`)
  },

  // Project roles
  listProjectRoles: async (projectId: string): Promise<Role[]> => {
    return apiGet<Role[]>(`/api/v1/projects/${projectId}/roles`)
  },

  createProjectRole: async (
    projectId: string,
    data: { name: string; description?: string; permissions: string[] }
  ): Promise<Role> => {
    return apiPost<Role>(`/api/v1/projects/${projectId}/roles`, data)
  },

  updateProjectRole: async (
    projectId: string,
    roleId: string,
    data: { name?: string; description?: string; permissions?: string[] }
  ): Promise<Role> => {
    return apiPatch<Role>(`/api/v1/projects/${projectId}/roles/${roleId}`, data)
  },

  deleteProjectRole: async (projectId: string, roleId: string): Promise<void> => {
    await apiDelete(`/api/v1/projects/${projectId}/roles/${roleId}`)
  },

  // Project role assignments
  grantProjectRole: async (
    projectId: string,
    userId: string,
    roleId: string
  ): Promise<UserProjectRole> => {
    return apiPost<UserProjectRole>(
      `/api/v1/projects/${projectId}/members/${userId}/roles`,
      { role_id: roleId }
    )
  },

  revokeProjectRole: async (
    projectId: string,
    userId: string,
    roleId: string
  ): Promise<void> => {
    await apiDelete(`/api/v1/projects/${projectId}/members/${userId}/roles/${roleId}`)
  },
}
