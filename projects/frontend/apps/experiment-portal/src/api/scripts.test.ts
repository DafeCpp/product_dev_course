import { describe, it, expect, vi, beforeEach } from 'vitest'

const { mockAxiosInstance, mockCreate } = vi.hoisted(() => {
    const instance = {
        get: vi.fn(),
        post: vi.fn(),
        patch: vi.fn(),
        delete: vi.fn(),
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

import { scriptsApi } from './scripts'

describe('scriptsApi', () => {
    beforeEach(() => {
        vi.clearAllMocks()
    })

    describe('listScripts', () => {
        it('GETs /api/v1/scripts with target_service/is_active/limit/offset', async () => {
            mockAxiosInstance.get.mockResolvedValueOnce({
                data: { scripts: [], total: 0 },
            })

            await scriptsApi.listScripts({
                target_service: 'experiment-service',
                is_active: true,
                limit: 50,
                offset: 0,
            })

            expect(mockAxiosInstance.get).toHaveBeenCalledWith('/api/v1/scripts', {
                params: {
                    target_service: 'experiment-service',
                    is_active: true,
                    limit: 50,
                    offset: 0,
                },
            })
        })

        it('GETs /api/v1/scripts with empty params object when none supplied', async () => {
            mockAxiosInstance.get.mockResolvedValueOnce({
                data: { scripts: [], total: 0 },
            })

            await scriptsApi.listScripts()

            expect(mockAxiosInstance.get).toHaveBeenCalledWith('/api/v1/scripts', {
                params: {},
            })
        })
    })

    describe('createScript', () => {
        it('POSTs payload to /api/v1/scripts', async () => {
            const payload = {
                name: 'cleanup',
                target_service: 'experiment-service',
                script_body: '#!/bin/bash\necho hi',
            }
            mockAxiosInstance.post.mockResolvedValueOnce({
                data: { id: 's1', ...payload },
            })

            const result = await scriptsApi.createScript(payload as any)

            expect(mockAxiosInstance.post).toHaveBeenCalledWith(
                '/api/v1/scripts',
                payload
            )
            expect(result).toMatchObject({ id: 's1' })
        })
    })

    describe('getScript', () => {
        it('GETs /api/v1/scripts/{id}', async () => {
            mockAxiosInstance.get.mockResolvedValueOnce({
                data: { id: 's1', name: 'cleanup' },
            })

            const result = await scriptsApi.getScript('s1')

            expect(mockAxiosInstance.get).toHaveBeenCalledWith('/api/v1/scripts/s1')
            expect(result).toMatchObject({ id: 's1' })
        })
    })

    describe('updateScript', () => {
        it('PATCHes /api/v1/scripts/{id} with payload', async () => {
            const upd = { name: 'cleanup-v2' }
            mockAxiosInstance.patch.mockResolvedValueOnce({
                data: { id: 's1', ...upd },
            })

            await scriptsApi.updateScript('s1', upd as any)

            expect(mockAxiosInstance.patch).toHaveBeenCalledWith(
                '/api/v1/scripts/s1',
                upd
            )
        })
    })

    describe('deleteScript', () => {
        it('DELETEs /api/v1/scripts/{id}', async () => {
            mockAxiosInstance.delete.mockResolvedValueOnce({ data: {} })

            await scriptsApi.deleteScript('s1')

            expect(mockAxiosInstance.delete).toHaveBeenCalledWith('/api/v1/scripts/s1')
        })
    })

    describe('executeScript', () => {
        it('POSTs to /api/v1/scripts/{id}/execute with parameters/target_instance', async () => {
            const exec = {
                id: 'e1',
                script_id: 's1',
                status: 'pending',
            }
            mockAxiosInstance.post.mockResolvedValueOnce({ data: exec })

            const params = {
                parameters: { foo: 'bar' },
                target_instance: 'experiment-service-1',
            }

            const result = await scriptsApi.executeScript('s1', params)

            expect(mockAxiosInstance.post).toHaveBeenCalledWith(
                '/api/v1/scripts/s1/execute',
                params
            )
            expect(result).toMatchObject({ id: 'e1' })
        })

        it('POSTs empty parameters object when none provided', async () => {
            mockAxiosInstance.post.mockResolvedValueOnce({
                data: { id: 'e1', status: 'pending' },
            })

            await scriptsApi.executeScript('s1', {})

            expect(mockAxiosInstance.post).toHaveBeenCalledWith(
                '/api/v1/scripts/s1/execute',
                {}
            )
        })
    })

    describe('listExecutions', () => {
        it('GETs /api/v1/executions with filter params', async () => {
            mockAxiosInstance.get.mockResolvedValueOnce({
                data: { executions: [], total: 0 },
            })

            await scriptsApi.listExecutions({
                script_id: 's1',
                status: 'running',
                requested_by: 'u1',
                limit: 20,
                offset: 0,
            })

            expect(mockAxiosInstance.get).toHaveBeenCalledWith('/api/v1/executions', {
                params: {
                    script_id: 's1',
                    status: 'running',
                    requested_by: 'u1',
                    limit: 20,
                    offset: 0,
                },
            })
        })
    })

    describe('getExecution', () => {
        it('GETs /api/v1/executions/{id}', async () => {
            mockAxiosInstance.get.mockResolvedValueOnce({
                data: { id: 'e1', status: 'succeeded' },
            })

            const result = await scriptsApi.getExecution('e1')

            expect(mockAxiosInstance.get).toHaveBeenCalledWith('/api/v1/executions/e1')
            expect(result).toMatchObject({ id: 'e1' })
        })
    })

    describe('cancelExecution', () => {
        it('POSTs to /api/v1/executions/{id}/cancel', async () => {
            mockAxiosInstance.post.mockResolvedValueOnce({ data: {} })

            await scriptsApi.cancelExecution('e1')

            expect(mockAxiosInstance.post).toHaveBeenCalledWith(
                '/api/v1/executions/e1/cancel'
            )
        })
    })
})
