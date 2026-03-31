import { apiGet, apiPost, apiPatch, apiDelete } from './client'
import type {
  Permission,
  Role,
  EffectivePermissions,
  GrantRoleRequest,
} from '../types/permissions'

export const permissionsApi = {
  getEffectivePermissions: async (
    userId: string,
    projectId?: string
  ): Promise<EffectivePermissions> => {
    return await apiGet(`/api/v1/users/${userId}/effective-permissions`, {
      params: projectId ? { project_id: projectId } : undefined,
    })
  },

  listPermissions: async (): Promise<Permission[]> => {
    return await apiGet('/api/v1/permissions')
  },

  // System roles
  listSystemRoles: async (): Promise<Role[]> => {
    return await apiGet('/api/v1/system-roles')
  },

  createSystemRole: async (data: {
    name: string
    description?: string | null
    permission_ids?: string[]
  }): Promise<Role> => {
    return await apiPost('/api/v1/system-roles', data)
  },

  updateSystemRole: async (
    id: string,
    data: { name?: string; description?: string | null; permission_ids?: string[] }
  ): Promise<Role> => {
    return await apiPatch(`/api/v1/system-roles/${id}`, data)
  },

  deleteSystemRole: async (id: string): Promise<void> => {
    await apiDelete(`/api/v1/system-roles/${id}`)
  },

  // Project roles
  listProjectRoles: async (projectId: string): Promise<Role[]> => {
    return await apiGet(`/api/v1/projects/${projectId}/roles`)
  },

  createProjectRole: async (
    projectId: string,
    data: { name: string; description?: string | null; permission_ids?: string[] }
  ): Promise<Role> => {
    return await apiPost(`/api/v1/projects/${projectId}/roles`, data)
  },

  updateProjectRole: async (
    projectId: string,
    roleId: string,
    data: { name?: string; description?: string | null; permission_ids?: string[] }
  ): Promise<Role> => {
    return await apiPatch(`/api/v1/projects/${projectId}/roles/${roleId}`, data)
  },

  deleteProjectRole: async (projectId: string, roleId: string): Promise<void> => {
    await apiDelete(`/api/v1/projects/${projectId}/roles/${roleId}`)
  },

  // User system role assignments
  grantSystemRole: async (userId: string, req: GrantRoleRequest): Promise<void> => {
    await apiPost(`/api/v1/users/${userId}/system-roles`, req)
  },

  revokeSystemRole: async (userId: string, roleId: string): Promise<void> => {
    await apiDelete(`/api/v1/users/${userId}/system-roles/${roleId}`)
  },

  // User project role assignments
  grantProjectRole: async (
    projectId: string,
    userId: string,
    req: GrantRoleRequest
  ): Promise<void> => {
    await apiPost(`/api/v1/projects/${projectId}/members/${userId}/roles`, req)
  },

  revokeProjectRole: async (
    projectId: string,
    userId: string,
    roleId: string
  ): Promise<void> => {
    await apiDelete(`/api/v1/projects/${projectId}/members/${userId}/roles/${roleId}`)
  },
}
