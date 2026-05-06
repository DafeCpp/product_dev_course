/** Runs API */
import type {
  Run,
  RunCreate,
  RunUpdate,
  RunsListResponse,
} from '../types'
import { apiGet, apiPost, apiPatch, apiClient } from './client'
import { getActiveProjectId } from '../utils/activeProject'

export const runsApi = {
  list: async (
    experimentId: string,
    params?: {
      status?: string
      page?: number
      page_size?: number
    }
  ): Promise<RunsListResponse> => {
    return await apiGet(`/api/v1/experiments/${experimentId}/runs`, {
      params,
    })
  },

  get: async (id: string): Promise<Run> => {
    return await apiGet(`/api/v1/runs/${id}`)
  },

  create: async (experimentId: string, data: RunCreate): Promise<Run> => {
    return await apiPost(`/api/v1/experiments/${experimentId}/runs`, data)
  },

  update: async (id: string, data: RunUpdate): Promise<Run> => {
    return await apiPatch(`/api/v1/runs/${id}`, data)
  },

  complete: async (id: string): Promise<Run> => {
    return await apiPatch(`/api/v1/runs/${id}`, { status: 'succeeded' })
  },

  fail: async (id: string, reason?: string): Promise<Run> => {
    return await apiPatch(`/api/v1/runs/${id}`, { status: 'failed', reason })
  },

  exportData: async (
    experimentId: string,
    params?: {
      format?: 'csv' | 'json'
      status?: string
      tags?: string
      created_after?: string
      created_before?: string
    }
  ): Promise<string> => {
    const response = await apiClient.get(`/api/v1/experiments/${experimentId}/runs/export`, {
      params: { ...params, project_id: getActiveProjectId() || undefined },
      responseType: 'text',
    })
    return response.data
  },

  bulkTags: async (args: {
    run_ids: string[]
    set_tags?: string[]
    add_tags?: string[]
    remove_tags?: string[]
  }): Promise<{ runs: Run[] }> => {
    return await apiPost('/api/v1/runs:bulk-tags', args)
  },
}
