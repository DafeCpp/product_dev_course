import axios from 'axios'
import type { AuditLogResponse } from '../types/permissions'

const AUTH_PROXY_URL = import.meta.env.VITE_AUTH_PROXY_URL ?? 'http://localhost:8080'

const client = axios.create({
  baseURL: AUTH_PROXY_URL,
  headers: { 'Content-Type': 'application/json' },
  withCredentials: true,
})

export interface AuditLogFilters {
  actor_id?: string
  action?: string
  scope_type?: 'system' | 'project'
  scope_id?: string
  target_type?: string
  target_id?: string
  from?: string
  to?: string
  limit?: number
  offset?: number
}

export const auditApi = {
  queryAuditLog: async (filters: AuditLogFilters = {}): Promise<AuditLogResponse> => {
    const params: Record<string, string | number> = {}
    if (filters.actor_id) params.actor_id = filters.actor_id
    if (filters.action) params.action = filters.action
    if (filters.scope_type) params.scope_type = filters.scope_type
    if (filters.scope_id) params.scope_id = filters.scope_id
    if (filters.target_type) params.target_type = filters.target_type
    if (filters.target_id) params.target_id = filters.target_id
    if (filters.from) params.from = filters.from
    if (filters.to) params.to = filters.to
    if (filters.limit !== undefined) params.limit = filters.limit
    if (filters.offset !== undefined) params.offset = filters.offset
    const res = await client.get<AuditLogResponse>('/api/v1/audit-log', { params })
    return res.data
  },
}
