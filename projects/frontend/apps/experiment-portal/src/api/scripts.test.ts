import { describe, it, expect, vi, beforeEach } from 'vitest'

const { mockClient } = vi.hoisted(() => ({
    mockClient: {
        get: vi.fn(),
        post: vi.fn(),
        patch: vi.fn(),
        delete: vi.fn(),
    },
}))

vi.mock('./http/axiosInstance', () => ({
    createAuthProxyClient: () => mockClient,
}))

import { scriptsApi } from './scripts'

describe('scriptsApi', () => {
    beforeEach(() => {
        vi.clearAllMocks()
    })

    describe('listScripts', () => {
        it('calls GET /api/v1/scripts with filter params', async () => {
            mockClient.get.mockResolvedValueOnce({ data: { scripts: [], total: 0 } })
            await scriptsApi.listScripts({ target_service: 'auth', is_active: true, limit: 50, offset: 0 })
            expect(mockClient.get).toHaveBeenCalledWith('/api/v1/scripts', {
                params: { target_service: 'auth', is_active: true, limit: 50, offset: 0 },
            })
        })

        it('uses default empty params', async () => {
            mockClient.get.mockResolvedValueOnce({ data: { scripts: [], total: 0 } })
            await scriptsApi.listScripts()
            expect(mockClient.get).toHaveBeenCalledWith('/api/v1/scripts', { params: {} })
        })
    })

    describe('createScript', () => {
        it('calls POST /api/v1/scripts with data', async () => {
            const data = {
                name: 'My Script',
                target_service: 'auth',
                code: 'print("hi")',
            }
            mockClient.post.mockResolvedValueOnce({ data: { id: 's1', ...data } })
            const result = await scriptsApi.createScript(data as any)
            expect(mockClient.post).toHaveBeenCalledWith('/api/v1/scripts', data)
            expect(result).toEqual({ id: 's1', ...data })
        })
    })

    describe('getScript', () => {
        it('calls GET /api/v1/scripts/{id}', async () => {
            mockClient.get.mockResolvedValueOnce({ data: { id: 's1' } })
            await scriptsApi.getScript('s1')
            expect(mockClient.get).toHaveBeenCalledWith('/api/v1/scripts/s1')
        })
    })

    describe('updateScript', () => {
        it('calls PATCH /api/v1/scripts/{id} with data', async () => {
            mockClient.patch.mockResolvedValueOnce({ data: { id: 's1', is_active: false } })
            await scriptsApi.updateScript('s1', { is_active: false } as any)
            expect(mockClient.patch).toHaveBeenCalledWith('/api/v1/scripts/s1', { is_active: false })
        })
    })

    describe('deleteScript', () => {
        it('calls DELETE /api/v1/scripts/{id}', async () => {
            mockClient.delete.mockResolvedValueOnce({})
            await scriptsApi.deleteScript('s1')
            expect(mockClient.delete).toHaveBeenCalledWith('/api/v1/scripts/s1')
        })
    })

    describe('executeScript', () => {
        it('calls POST /api/v1/scripts/{id}/execute with parameters', async () => {
            const params = { parameters: { foo: 'bar' }, target_instance: 'auth-1' }
            mockClient.post.mockResolvedValueOnce({ data: { id: 'exec-1', status: 'running' } })
            await scriptsApi.executeScript('s1', params)
            expect(mockClient.post).toHaveBeenCalledWith('/api/v1/scripts/s1/execute', params)
        })
    })

    describe('listExecutions', () => {
        it('calls GET /api/v1/executions with filters', async () => {
            mockClient.get.mockResolvedValueOnce({ data: { executions: [], total: 0 } })
            await scriptsApi.listExecutions({ script_id: 's1', status: 'running', limit: 25, offset: 0 })
            expect(mockClient.get).toHaveBeenCalledWith('/api/v1/executions', {
                params: { script_id: 's1', status: 'running', limit: 25, offset: 0 },
            })
        })
    })

    describe('getExecution', () => {
        it('calls GET /api/v1/executions/{id}', async () => {
            mockClient.get.mockResolvedValueOnce({ data: { id: 'exec-1' } })
            await scriptsApi.getExecution('exec-1')
            expect(mockClient.get).toHaveBeenCalledWith('/api/v1/executions/exec-1')
        })
    })

    describe('cancelExecution', () => {
        it('calls POST /api/v1/executions/{id}/cancel', async () => {
            mockClient.post.mockResolvedValueOnce({})
            await scriptsApi.cancelExecution('exec-1')
            expect(mockClient.post).toHaveBeenCalledWith('/api/v1/executions/exec-1/cancel')
        })

        it('propagates errors', async () => {
            mockClient.post.mockRejectedValueOnce(new Error('not cancellable'))
            await expect(scriptsApi.cancelExecution('exec-1')).rejects.toThrow('not cancellable')
        })
    })
})
