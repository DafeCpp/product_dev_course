import { apiGet, apiPost, apiPatch, apiDelete } from './client'
import type { Script, ScriptExecution } from '../types/scripts'

interface ScriptListParams {
  is_active?: boolean
  target_service?: string
  limit?: number
  offset?: number
}

interface ScriptExecutionListParams {
  script_id?: string
  status?: string
  limit?: number
  offset?: number
}

export const scriptsApi = {
  listScripts: async (params?: ScriptListParams): Promise<Script[]> => {
    return await apiGet('/api/v1/scripts', { params })
  },

  getScript: async (id: string): Promise<Script> => {
    return await apiGet(`/api/v1/scripts/${id}`)
  },

  createScript: async (data: {
    name: string
    description?: string | null
    target_service: string
    script_type: Script['script_type']
    script_body: string
    parameters_schema?: Record<string, unknown>
    timeout_sec?: number
    is_active?: boolean
  }): Promise<Script> => {
    return await apiPost('/api/v1/scripts', data)
  },

  updateScript: async (
    id: string,
    data: {
      name?: string
      description?: string | null
      target_service?: string
      script_type?: Script['script_type']
      script_body?: string
      parameters_schema?: Record<string, unknown>
      timeout_sec?: number
      is_active?: boolean
    }
  ): Promise<Script> => {
    return await apiPatch(`/api/v1/scripts/${id}`, data)
  },

  deleteScript: async (id: string): Promise<void> => {
    await apiDelete(`/api/v1/scripts/${id}`)
  },

  executeScript: async (
    id: string,
    data: { parameters?: Record<string, unknown>; target_instance?: string }
  ): Promise<ScriptExecution> => {
    return await apiPost(`/api/v1/scripts/${id}/execute`, data)
  },

  cancelExecution: async (id: string): Promise<void> => {
    await apiPost(`/api/v1/script-executions/${id}/cancel`)
  },

  listExecutions: async (params?: ScriptExecutionListParams): Promise<ScriptExecution[]> => {
    return await apiGet('/api/v1/script-executions', { params })
  },

  getExecution: async (id: string): Promise<ScriptExecution> => {
    return await apiGet(`/api/v1/script-executions/${id}`)
  },
}
