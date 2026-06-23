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

import { runsApi } from './runs'
import { setActiveProjectId } from '../utils/activeProject'

const PROJECT_ID = 'project-1'

describe('runsApi', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        setActiveProjectId(PROJECT_ID)
    })

    describe('list', () => {
        it('calls GET /api/v1/experiments/{id}/runs with status/page params and active project_id', async () => {
            const mockResponse = { data: { runs: [], total: 0, page: 1, page_size: 20 } }
            mockAxiosInstance.get.mockResolvedValueOnce(mockResponse)

            const result = await runsApi.list('exp-1', { status: 'running', page: 2, page_size: 50 })

            expect(mockAxiosInstance.get).toHaveBeenCalledWith(
                '/api/v1/experiments/exp-1/runs',
                expect.objectContaining({
                    params: expect.objectContaining({
                        status: 'running',
                        page: 2,
                        page_size: 50,
                        project_id: PROJECT_ID,
                    }),
                })
            )
            expect(result).toEqual(mockResponse.data)
        })

        it('propagates API errors', async () => {
            mockAxiosInstance.get.mockRejectedValueOnce(new Error('boom'))
            await expect(runsApi.list('exp-1')).rejects.toThrow('boom')
        })
    })

    describe('get', () => {
        it('calls GET /api/v1/runs/{id}', async () => {
            const run = { id: 'run-1', experiment_id: 'exp-1', status: 'running' }
            mockAxiosInstance.get.mockResolvedValueOnce({ data: run })

            const result = await runsApi.get('run-1')

            expect(mockAxiosInstance.get).toHaveBeenCalledWith(
                '/api/v1/runs/run-1',
                expect.objectContaining({
                    params: expect.objectContaining({ project_id: PROJECT_ID }),
                })
            )
            expect(result).toEqual(run)
        })
    })

    describe('create', () => {
        it('calls POST /api/v1/experiments/{id}/runs with payload', async () => {
            const payload = { name: 'Run-1', config: { foo: 'bar' } }
            const created = { id: 'run-1', ...payload, status: 'created' }
            mockAxiosInstance.post.mockResolvedValueOnce({ data: created })

            const result = await runsApi.create('exp-1', payload as any)

            expect(mockAxiosInstance.post).toHaveBeenCalledWith(
                '/api/v1/experiments/exp-1/runs',
                payload,
                expect.objectContaining({
                    params: expect.objectContaining({ project_id: PROJECT_ID }),
                })
            )
            expect(result).toEqual(created)
        })
    })

    describe('update', () => {
        it('calls PATCH /api/v1/runs/{id} with update payload', async () => {
            const upd = { tags: ['flaky'] }
            const run = { id: 'run-1', status: 'running', tags: ['flaky'] }
            mockAxiosInstance.patch.mockResolvedValueOnce({ data: run })

            const result = await runsApi.update('run-1', upd as any)

            expect(mockAxiosInstance.patch).toHaveBeenCalledWith(
                '/api/v1/runs/run-1',
                upd,
                expect.any(Object)
            )
            expect(result).toEqual(run)
        })
    })

    describe('complete', () => {
        it('PATCHes status=succeeded', async () => {
            mockAxiosInstance.patch.mockResolvedValueOnce({ data: { id: 'run-1', status: 'succeeded' } })

            await runsApi.complete('run-1')

            expect(mockAxiosInstance.patch).toHaveBeenCalledWith(
                '/api/v1/runs/run-1',
                { status: 'succeeded' },
                expect.any(Object)
            )
        })
    })

    describe('fail', () => {
        it('PATCHes status=failed with reason', async () => {
            mockAxiosInstance.patch.mockResolvedValueOnce({ data: { id: 'run-1', status: 'failed' } })

            await runsApi.fail('run-1', 'sensor offline')

            expect(mockAxiosInstance.patch).toHaveBeenCalledWith(
                '/api/v1/runs/run-1',
                { status: 'failed', reason: 'sensor offline' },
                expect.any(Object)
            )
        })

        it('PATCHes status=failed without reason', async () => {
            mockAxiosInstance.patch.mockResolvedValueOnce({ data: { id: 'run-1', status: 'failed' } })

            await runsApi.fail('run-1')

            expect(mockAxiosInstance.patch).toHaveBeenCalledWith(
                '/api/v1/runs/run-1',
                { status: 'failed', reason: undefined },
                expect.any(Object)
            )
        })
    })

    describe('exportData', () => {
        it('calls GET /api/v1/experiments/{id}/runs/export with text responseType and merges project_id', async () => {
            mockAxiosInstance.get.mockResolvedValueOnce({ data: 'col1,col2\n1,2\n' })

            const result = await runsApi.exportData('exp-1', {
                format: 'csv',
                status: 'succeeded',
                tags: 'baseline',
            })

            expect(mockAxiosInstance.get).toHaveBeenCalledWith(
                '/api/v1/experiments/exp-1/runs/export',
                expect.objectContaining({
                    params: expect.objectContaining({
                        format: 'csv',
                        status: 'succeeded',
                        tags: 'baseline',
                        project_id: PROJECT_ID,
                    }),
                    responseType: 'text',
                })
            )
            expect(result).toBe('col1,col2\n1,2\n')
        })
    })

    describe('bulkTags', () => {
        it('calls POST /api/v1/runs:bulk-tags', async () => {
            const args = { run_ids: ['run-1', 'run-2'], add_tags: ['baseline'] }
            const updated = { runs: [{ id: 'run-1' }, { id: 'run-2' }] }
            mockAxiosInstance.post.mockResolvedValueOnce({ data: updated })

            const result = await runsApi.bulkTags(args)

            expect(mockAxiosInstance.post).toHaveBeenCalledWith(
                '/api/v1/runs:bulk-tags',
                args,
                expect.any(Object)
            )
            expect(result).toEqual(updated)
        })
    })
})
