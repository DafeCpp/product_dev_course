/** Experiments API */
import type {
  Experiment,
  ExperimentCreate,
  ExperimentUpdate,
  ExperimentsListResponse,
} from '../types'
import { apiGet, apiPost, apiPatch, apiDelete, apiClient } from './client'
import { getActiveProjectId } from '../utils/activeProject'

export const experimentsApi = {
  list: async (params?: {
    project_id?: string
    status?: string
    tags?: string
    page?: number
    page_size?: number
  }): Promise<ExperimentsListResponse> => {
    return await apiGet('/api/v1/experiments', { params })
  },

  get: async (id: string): Promise<Experiment> => {
    return await apiGet(`/api/v1/experiments/${id}`)
  },

  create: async (data: ExperimentCreate): Promise<Experiment> => {
    return await apiPost('/api/v1/experiments', data)
  },

  update: async (id: string, data: ExperimentUpdate): Promise<Experiment> => {
    return await apiPatch(`/api/v1/experiments/${id}`, data)
  },

  archive: async (id: string, params?: { project_id?: string }): Promise<Experiment> => {
    return await apiPost(`/api/v1/experiments/${id}/archive`, {}, { params })
  },

  delete: async (id: string): Promise<void> => {
    await apiDelete(`/api/v1/experiments/${id}`)
  },

  search: async (params: {
    q?: string
    project_id?: string
    page?: number
    page_size?: number
  }): Promise<ExperimentsListResponse> => {
    return await apiGet('/api/v1/experiments/search', { params })
  },

  exportData: async (params: {
    project_id?: string
    format?: 'csv' | 'json'
    status?: string
    tags?: string
    created_after?: string
    created_before?: string
  }): Promise<string> => {
    const response = await apiClient.get('/api/v1/experiments/export', {
      params: { ...params, project_id: params.project_id || getActiveProjectId() },
      responseType: 'text',
    })
    return response.data
  },
}
