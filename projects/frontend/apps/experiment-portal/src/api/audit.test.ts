import { describe, it, expect, vi, beforeEach } from 'vitest'

const { mockAxiosInstance, mockCreate } = vi.hoisted(() => {
    const instance = {
        get: vi.fn(),
        post: vi.fn(),
        interceptors: {
            request: { use: vi.fn() },
            response: { use: vi.fn() },
        },
    }
    const create = vi.fn(() => instance)
    return { mockAxiosInstance: instance, mockCreate: create }
})

vi.mock('axios', () => ({
    default: { create: mockCreate, post: vi.fn() },
}))

import { auditApi } from './audit'
import { setActiveProjectId } from '../utils/activeProject'

describe('auditApi.queryAuditLog', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        setActiveProjectId('project-1')
    })

    it('GETs /api/v1/audit-log with no filters set', async () => {
        mockAxiosInstance.get.mockResolvedValueOnce({ data: { entries: [], total: 0 } })

        await auditApi.queryAuditLog()

        const call = mockAxiosInstance.get.mock.calls[0]
        expect(call[0]).toBe('/api/v1/audit-log')
        const params = call[1].params
        // No filters → only project_id is injected by client helper
        expect(params).toMatchObject({ project_id: 'project-1' })
        expect(params.actor_id).toBeUndefined()
    })

    it('forwards every supported filter to params', async () => {
        mockAxiosInstance.get.mockResolvedValueOnce({ data: { entries: [], total: 0 } })

        await auditApi.queryAuditLog({
            actor_id: 'u1',
            action: 'role.grant',
            scope_type: 'project',
            scope_id: 'p1',
            target_type: 'user',
            target_id: 'u2',
            from: '2025-01-01T00:00:00Z',
            to: '2025-12-31T23:59:59Z',
            limit: 100,
            offset: 50,
        })

        expect(mockAxiosInstance.get).toHaveBeenCalledWith(
            '/api/v1/audit-log',
            expect.objectContaining({
                params: expect.objectContaining({
                    actor_id: 'u1',
                    action: 'role.grant',
                    scope_type: 'project',
                    scope_id: 'p1',
                    target_type: 'user',
                    target_id: 'u2',
                    from: '2025-01-01T00:00:00Z',
                    to: '2025-12-31T23:59:59Z',
                    limit: 100,
                    offset: 50,
                }),
            })
        )
    })

    it('omits filter keys whose values are falsy/empty', async () => {
        mockAxiosInstance.get.mockResolvedValueOnce({ data: { entries: [], total: 0 } })

        await auditApi.queryAuditLog({
            actor_id: '',
            action: undefined,
            limit: 10,
        })

        const params = mockAxiosInstance.get.mock.calls[0][1].params
        expect(params.actor_id).toBeUndefined()
        expect(params.action).toBeUndefined()
        expect(params.limit).toBe(10)
    })

    it('keeps offset=0 in params (does not treat 0 as missing)', async () => {
        mockAxiosInstance.get.mockResolvedValueOnce({ data: { entries: [], total: 0 } })

        await auditApi.queryAuditLog({ offset: 0, limit: 0 })

        const params = mockAxiosInstance.get.mock.calls[0][1].params
        expect(params.offset).toBe(0)
        expect(params.limit).toBe(0)
    })

    it('returns the parsed response payload', async () => {
        const payload = {
            entries: [{ id: 'a1', actor_id: 'u1' }],
            total: 1,
        }
        mockAxiosInstance.get.mockResolvedValueOnce({ data: payload })

        const result = await auditApi.queryAuditLog()
        expect(result).toEqual(payload)
    })
})
