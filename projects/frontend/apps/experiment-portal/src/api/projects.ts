/** Projects and Users API */
import type {
  Project,
  ProjectCreate,
  ProjectUpdate,
  ProjectsListResponse,
  ProjectMember,
  ProjectMemberAdd,
  ProjectMemberUpdate,
  ProjectMembersListResponse,
} from '../types'
import { apiGet, apiClient } from './client'

export const projectsApi = {
  list: async (params?: {
    search?: string
    role?: string
    limit?: number
    offset?: number
  }): Promise<ProjectsListResponse> => {
    const response = await apiClient.get<{ items: Project[]; total: number }>('/projects', { params })
    return { projects: response.data.items ?? [], total: response.data.total }
  },

  get: async (id: string): Promise<Project> => {
    const response = await apiClient.get<Project>(`/projects/${id}`)
    return response.data
  },

  create: async (data: ProjectCreate): Promise<Project> => {
    const response = await apiClient.post<Project>('/projects', data)
    return response.data
  },

  update: async (id: string, data: ProjectUpdate): Promise<Project> => {
    const response = await apiClient.put<Project>(`/projects/${id}`, data)
    return response.data
  },

  delete: async (id: string): Promise<void> => {
    await apiClient.delete(`/projects/${id}`)
  },

  listMembers: async (projectId: string): Promise<ProjectMembersListResponse> => {
    const response = await apiClient.get<ProjectMembersListResponse>(
      `/projects/${projectId}/members`
    )
    return response.data
  },

  addMember: async (projectId: string, data: ProjectMemberAdd): Promise<ProjectMember> => {
    const response = await apiClient.post<ProjectMember>(
      `/projects/${projectId}/members`,
      data
    )
    return response.data
  },

  removeMember: async (projectId: string, userId: string): Promise<void> => {
    await apiClient.delete(`/projects/${projectId}/members/${userId}`)
  },

  updateMemberRole: async (
    projectId: string,
    userId: string,
    data: ProjectMemberUpdate
  ): Promise<ProjectMember> => {
    const response = await apiClient.put<ProjectMember>(
      `/projects/${projectId}/members/${userId}/role`,
      data
    )
    return response.data
  },
}

export const usersApi = {
  search: async (params: {
    q: string
    exclude_project_id?: string
  }): Promise<{ users: Array<{ id: string; username: string; email: string }> }> => {
    const data = await apiGet<Array<{ id: string; username: string; email: string }>>(
      '/api/v1/users/search',
      { params },
    )
    return { users: Array.isArray(data) ? data : [] }
  },
}
