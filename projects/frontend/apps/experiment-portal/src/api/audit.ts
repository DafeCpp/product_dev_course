import { apiGet } from './client'
import type { AuditEntry } from '../types/permissions'

interface AuditLogQuery {
  actor_id?: string
  action?: string
  scope_type?: string
  scope_id?: string
  target_type?: string
  target_id?: string
  from?: string
  to?: string
  limit?: number
  offset?: number
}

export const auditApi = {
  listAuditLog: async (
    query?: AuditLogQuery
  ): Promise<{ items: AuditEntry[]; total: number }> => {
    return await apiGet('/api/v1/audit-log', { params: query })
  },
}
