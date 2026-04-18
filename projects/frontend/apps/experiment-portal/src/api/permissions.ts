import axios from 'axios'
import type {
  Permission,
  Role,
  EffectivePermissions,
  UserSystemRole,
  UserProjectRole,
} from '../types/permissions'

const AUTH_PROXY_URL = import.meta.env.VITE_AUTH_PROXY_URL ?? 'http://localhost:8080'

const client = axios.create({
  baseURL: AUTH_PROXY_URL,
  headers: { 'Content-Type': 'application/json' },
  withCredentials: true,
})

export const permissionsApi = {
  // Permission catalog
  listPermissions: async (): Promise<Permission[]> => {
    const res = await client.get<Permission[]>('/api/v1/permissions')
    return res.data
  },

  // Effective permissions for a user
  getEffectivePermissions: async (
    userId: string,
    projectId?: string
  ): Promise<EffectivePermissions> => {
    const params = projectId ? { project_id: projectId } : {}
    const res = await client.get<EffectivePermissions>(
      `/api/v1/users/${userId}/effective-permissions`,
      { params }
    )
    return res.data
  },

  // System roles
  listSystemRoles: async (): Promise<Role[]> => {
    const res = await client.get<Role[]>('/api/v1/system-roles')
    return res.data
  },

  createSystemRole: async (data: {
    name: string
    description?: string
    permissions: string[]
  }): Promise<Role> => {
    const res = await client.post<Role>('/api/v1/system-roles', data)
    return res.data
  },

  updateSystemRole: async (
    roleId: string,
    data: { name?: string; description?: string; permissions?: string[] }
  ): Promise<Role> => {
    const res = await client.patch<Role>(`/api/v1/system-roles/${roleId}`, data)
    return res.data
  },

  deleteSystemRole: async (roleId: string): Promise<void> => {
    await client.delete(`/api/v1/system-roles/${roleId}`)
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
    const res = await client.post<UserSystemRole>(`/api/v1/users/${userId}/system-roles`, body)
    return res.data
  },

  revokeSystemRole: async (userId: string, roleId: string): Promise<void> => {
    await client.delete(`/api/v1/users/${userId}/system-roles/${roleId}`)
  },

  // Project roles
  listProjectRoles: async (projectId: string): Promise<Role[]> => {
    const res = await client.get<Role[]>(`/api/v1/projects/${projectId}/roles`)
    return res.data
  },

  createProjectRole: async (
    projectId: string,
    data: { name: string; description?: string; permissions: string[] }
  ): Promise<Role> => {
    const res = await client.post<Role>(`/api/v1/projects/${projectId}/roles`, data)
    return res.data
  },

  updateProjectRole: async (
    projectId: string,
    roleId: string,
    data: { name?: string; description?: string; permissions?: string[] }
  ): Promise<Role> => {
    const res = await client.patch<Role>(
      `/api/v1/projects/${projectId}/roles/${roleId}`,
      data
    )
    return res.data
  },

  deleteProjectRole: async (projectId: string, roleId: string): Promise<void> => {
    await client.delete(`/api/v1/projects/${projectId}/roles/${roleId}`)
  },

  // Project role assignments
  grantProjectRole: async (
    projectId: string,
    userId: string,
    roleId: string
  ): Promise<UserProjectRole> => {
    const res = await client.post<UserProjectRole>(
      `/api/v1/projects/${projectId}/members/${userId}/roles`,
      { role_id: roleId }
    )
    return res.data
  },

  revokeProjectRole: async (
    projectId: string,
    userId: string,
    roleId: string
  ): Promise<void> => {
    await client.delete(
      `/api/v1/projects/${projectId}/members/${userId}/roles/${roleId}`
    )
  },
}
