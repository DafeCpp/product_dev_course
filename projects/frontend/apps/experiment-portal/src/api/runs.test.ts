import { describe, it, expect, vi, beforeEach } from 'vitest'

vi.mock('./client', () => ({
    apiGet: vi.fn(),
    apiPost: vi.fn(),
    apiPatch: vi.fn(),
    apiClient: {
        get: vi.fn(),
    },
}))

vi.mock('../utils/activeProject', () => ({
    getActiveProjectId: vi.fn(() => null),
}))

import { runsApi } from './runs'
import { apiGet, apiPost, apiPatch, apiClient } from './client'
import { getActiveProjectId } from '../utils/activeProject'

const mockApiGet = vi.mocked(apiGet)
const mockApiPost = vi.mocked(apiPost)
const mockApiPatch = vi.mocked(apiPatch)
const mockClientGet = vi.mocked(apiClient.get)
const mockGetActiveProjectId = vi.mocked(getActiveProjectId)

describe('runsApi', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        mockGetActiveProjectId.mockReturnValue(null)
    })

    describe('list', () => {
        it('calls GET /api/v1/experiments/{id}/runs with filters', async () => {
            mockApiGet.mockResolvedValueOnce({ runs: [], total: 0 })
            await runsApi.list('exp-1', { status: 'running', page: 1, page_size: 25 })
            expect(mockApiGet).toHaveBeenCalledWith('/api/v1/experiments/exp-1/runs', {
                params: { status: 'running', page: 1, page_size: 25 },
            })
        })

        it('passes undefined params when no filters', async () => {
            mockApiGet.mockResolvedValueOnce({ runs: [], total: 0 })
            await runsApi.list('exp-1')
            expect(mockApiGet).toHaveBeenCalledWith('/api/v1/experiments/exp-1/runs', {
                params: undefined,
            })
        })
    })

    describe('get', () => {
        it('calls GET /api/v1/runs/{id}', async () => {
            mockApiGet.mockResolvedValueOnce({ id: 'run-1' })
            await runsApi.get('run-1')
            expect(mockApiGet).toHaveBeenCalledWith('/api/v1/runs/run-1')
        })
    })

    describe('create', () => {
        it('calls POST /api/v1/experiments/{id}/runs with data', async () => {
            const data = { name: 'My Run' }
            mockApiPost.mockResolvedValueOnce({ id: 'run-1', ...data })
            await runsApi.create('exp-1', data as any)
            expect(mockApiPost).toHaveBeenCalledWith('/api/v1/experiments/exp-1/runs', data)
        })
    })

    describe('update', () => {
        it('calls PATCH /api/v1/runs/{id} with data', async () => {
            mockApiPatch.mockResolvedValueOnce({ id: 'run-1', name: 'Updated' })
            await runsApi.update('run-1', { name: 'Updated' } as any)
            expect(mockApiPatch).toHaveBeenCalledWith('/api/v1/runs/run-1', { name: 'Updated' })
        })
    })

    describe('complete', () => {
        it('calls PATCH /api/v1/runs/{id} with status=succeeded', async () => {
            mockApiPatch.mockResolvedValueOnce({ id: 'run-1', status: 'succeeded' })
            await runsApi.complete('run-1')
            expect(mockApiPatch).toHaveBeenCalledWith('/api/v1/runs/run-1', { status: 'succeeded' })
        })
    })

    describe('fail', () => {
        it('calls PATCH /api/v1/runs/{id} with status=failed and reason', async () => {
            mockApiPatch.mockResolvedValueOnce({ id: 'run-1', status: 'failed' })
            await runsApi.fail('run-1', 'OOM')
            expect(mockApiPatch).toHaveBeenCalledWith('/api/v1/runs/run-1', {
                status: 'failed',
                reason: 'OOM',
            })
        })

        it('omits reason when not provided', async () => {
            mockApiPatch.mockResolvedValueOnce({ id: 'run-1', status: 'failed' })
            await runsApi.fail('run-1')
            expect(mockApiPatch).toHaveBeenCalledWith('/api/v1/runs/run-1', {
                status: 'failed',
                reason: undefined,
            })
        })
    })

    describe('exportData', () => {
        it('calls apiClient.get with active project_id and responseType text', async () => {
            mockGetActiveProjectId.mockReturnValue('active-proj')
            mockClientGet.mockResolvedValueOnce({ data: 'csv,data' } as any)

            const result = await runsApi.exportData('exp-1', { format: 'csv' })

            expect(mockClientGet).toHaveBeenCalledWith('/api/v1/experiments/exp-1/runs/export', {
                params: { format: 'csv', project_id: 'active-proj' },
                responseType: 'text',
            })
            expect(result).toBe('csv,data')
        })

        it('omits project_id when no active project', async () => {
            mockGetActiveProjectId.mockReturnValue(null)
            mockClientGet.mockResolvedValueOnce({ data: 'data' } as any)

            await runsApi.exportData('exp-1')

            expect(mockClientGet).toHaveBeenCalledWith('/api/v1/experiments/exp-1/runs/export', {
                params: { project_id: undefined },
                responseType: 'text',
            })
        })
    })

    describe('bulkTags', () => {
        it('calls POST /api/v1/runs:bulk-tags with set/add/remove', async () => {
            const args = {
                run_ids: ['r1', 'r2'],
                set_tags: ['v2'],
                add_tags: ['ok'],
                remove_tags: ['old'],
            }
            mockApiPost.mockResolvedValueOnce({ runs: [] })
            await runsApi.bulkTags(args)
            expect(mockApiPost).toHaveBeenCalledWith('/api/v1/runs:bulk-tags', args)
        })
    })
})
