import { describe, it, expect, vi, beforeEach } from 'vitest'

vi.mock('./client', () => ({
    apiGet: vi.fn(),
}))

import { auditApi } from './audit'
import { apiGet } from './client'

const mockApiGet = vi.mocked(apiGet)

describe('auditApi.queryAuditLog', () => {
    beforeEach(() => {
        vi.clearAllMocks()
    })

    it('calls GET /api/v1/audit-log without filters when none provided', async () => {
        mockApiGet.mockResolvedValueOnce({ entries: [], total: 0 })
        await auditApi.queryAuditLog()
        expect(mockApiGet).toHaveBeenCalledWith('/api/v1/audit-log', { params: {} })
    })

    it('passes all filter params when provided', async () => {
        mockApiGet.mockResolvedValueOnce({ entries: [], total: 0 })
        await auditApi.queryAuditLog({
            actor_id: 'user-1',
            action: 'create',
            scope_type: 'project',
            scope_id: 'proj-1',
            target_type: 'experiment',
            target_id: 'exp-1',
            from: '2025-01-01',
            to: '2025-12-31',
            limit: 50,
            offset: 100,
        })
        expect(mockApiGet).toHaveBeenCalledWith('/api/v1/audit-log', {
            params: {
                actor_id: 'user-1',
                action: 'create',
                scope_type: 'project',
                scope_id: 'proj-1',
                target_type: 'experiment',
                target_id: 'exp-1',
                from: '2025-01-01',
                to: '2025-12-31',
                limit: 50,
                offset: 100,
            },
        })
    })

    it('omits empty filter fields from params', async () => {
        mockApiGet.mockResolvedValueOnce({ entries: [], total: 0 })
        await auditApi.queryAuditLog({ actor_id: 'user-1', limit: 10 })
        expect(mockApiGet).toHaveBeenCalledWith('/api/v1/audit-log', {
            params: { actor_id: 'user-1', limit: 10 },
        })
    })

    it('includes offset=0 (falsy but valid)', async () => {
        mockApiGet.mockResolvedValueOnce({ entries: [], total: 0 })
        await auditApi.queryAuditLog({ offset: 0 })
        expect(mockApiGet).toHaveBeenCalledWith('/api/v1/audit-log', {
            params: { offset: 0 },
        })
    })

    it('returns response data', async () => {
        const mockResponse = { entries: [{ id: 'e1', action: 'create' }], total: 1 }
        mockApiGet.mockResolvedValueOnce(mockResponse)
        const result = await auditApi.queryAuditLog({ action: 'create' })
        expect(result).toEqual(mockResponse)
    })

    it('propagates errors', async () => {
        mockApiGet.mockRejectedValueOnce(new Error('forbidden'))
        await expect(auditApi.queryAuditLog()).rejects.toThrow('forbidden')
    })
})
