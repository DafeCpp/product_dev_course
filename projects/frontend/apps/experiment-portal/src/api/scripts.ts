import axios from 'axios'
import type {
  Script,
  ScriptCreate,
  ScriptUpdate,
  ScriptExecution,
  ScriptsListResponse,
  ExecutionsListResponse,
} from '../types/scripts'

const AUTH_PROXY_URL = import.meta.env.VITE_AUTH_PROXY_URL ?? 'http://localhost:8080'

const client = axios.create({
  baseURL: AUTH_PROXY_URL,
  headers: { 'Content-Type': 'application/json' },
  withCredentials: true,
})

export interface ScriptsListParams {
  target_service?: string
  is_active?: boolean
  limit?: number
  offset?: number
}

export interface ExecutionsListParams {
  script_id?: string
  status?: string
  requested_by?: string
  limit?: number
  offset?: number
}

export const scriptsApi = {
  listScripts: async (params: ScriptsListParams = {}): Promise<ScriptsListResponse> => {
    const res = await client.get<ScriptsListResponse>('/api/v1/scripts', { params })
    return res.data
  },

  createScript: async (data: ScriptCreate): Promise<Script> => {
    const res = await client.post<Script>('/api/v1/scripts', data)
    return res.data
  },

  getScript: async (id: string): Promise<Script> => {
    const res = await client.get<Script>(`/api/v1/scripts/${id}`)
    return res.data
  },

  updateScript: async (id: string, data: ScriptUpdate): Promise<Script> => {
    const res = await client.patch<Script>(`/api/v1/scripts/${id}`, data)
    return res.data
  },

  deleteScript: async (id: string): Promise<void> => {
    await client.delete(`/api/v1/scripts/${id}`)
  },

  executeScript: async (
    id: string,
    params: { parameters?: Record<string, unknown>; target_instance?: string }
  ): Promise<ScriptExecution> => {
    const res = await client.post<ScriptExecution>(
      `/api/v1/scripts/${id}/execute`,
      params
    )
    return res.data
  },

  listExecutions: async (
    params: ExecutionsListParams = {}
  ): Promise<ExecutionsListResponse> => {
    const res = await client.get<ExecutionsListResponse>('/api/v1/executions', { params })
    return res.data
  },

  getExecution: async (id: string): Promise<ScriptExecution> => {
    const res = await client.get<ScriptExecution>(`/api/v1/executions/${id}`)
    return res.data
  },

  cancelExecution: async (id: string): Promise<void> => {
    await client.post(`/api/v1/executions/${id}/cancel`)
  },
}
