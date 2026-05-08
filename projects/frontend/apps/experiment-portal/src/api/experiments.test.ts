import { describe, it, expect, vi, beforeEach } from 'vitest'

vi.mock('./client', () => ({
    apiGet: vi.fn(),
    apiPost: vi.fn(),
    apiPatch: vi.fn(),
    apiDelete: vi.fn(),
    apiClient: {
        get: vi.fn(),
    },
}))

vi.mock('../utils/activeProject', () => ({
    getActiveProjectId: vi.fn(() => null),
}))

import { experimentsApi } from './experiments'
import { apiGet, apiPost, apiPatch, apiDelete, apiClient } from './client'
import { getActiveProjectId } from '../utils/activeProject'

const mockApiGet = vi.mocked(apiGet)
const mockApiPost = vi.mocked(apiPost)
const mockApiPatch = vi.mocked(apiPatch)
const mockApiDelete = vi.mocked(apiDelete)
const mockClientGet = vi.mocked(apiClient.get)
const mockGetActiveProjectId = vi.mocked(getActiveProjectId)

describe('experimentsApi', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        mockGetActiveProjectId.mockReturnValue(null)
    })

    describe('list', () => {
        it('calls GET /api/v1/experiments with filters', async () => {
            const mockResponse = { experiments: [], total: 0 }
            mockApiGet.mockResolvedValueOnce(mockResponse)

            const result = await experimentsApi.list({
                project_id: 'p1',
                status: 'active',
                tags: 'foo,bar',
                page: 2,
                page_size: 50,
            })

            expect(mockApiGet).toHaveBeenCalledWith('/api/v1/experiments', {
                params: {
                    project_id: 'p1',
                    status: 'active',
                    tags: 'foo,bar',
                    page: 2,
                    page_size: 50,
                },
            })
            expect(result).toEqual(mockResponse)
        })

        it('passes undefined params when none provided', async () => {
            mockApiGet.mockResolvedValueOnce({ experiments: [], total: 0 })
            await experimentsApi.list()
            expect(mockApiGet).toHaveBeenCalledWith('/api/v1/experiments', { params: undefined })
        })
    })

    describe('get', () => {
        it('calls GET /api/v1/experiments/{id}', async () => {
            const mockExp = { id: 'exp-1', name: 'Test' }
            mockApiGet.mockResolvedValueOnce(mockExp)
            const result = await experimentsApi.get('exp-1')
            expect(mockApiGet).toHaveBeenCalledWith('/api/v1/experiments/exp-1')
            expect(result).toEqual(mockExp)
        })
    })

    describe('create', () => {
        it('calls POST /api/v1/experiments with data', async () => {
            const data = { name: 'New Exp', project_id: 'p1' }
            const mockExp = { id: 'exp-1', ...data }
            mockApiPost.mockResolvedValueOnce(mockExp)

            const result = await experimentsApi.create(data as any)

            expect(mockApiPost).toHaveBeenCalledWith('/api/v1/experiments', data)
            expect(result).toEqual(mockExp)
        })

        it('propagates validation errors', async () => {
            mockApiPost.mockRejectedValueOnce(new Error('name required'))
            await expect(experimentsApi.create({} as any)).rejects.toThrow('name required')
        })
    })

    describe('update', () => {
        it('calls PATCH /api/v1/experiments/{id}', async () => {
            const data = { name: 'Updated' }
            mockApiPatch.mockResolvedValueOnce({ id: 'exp-1', ...data })
            await experimentsApi.update('exp-1', data as any)
            expect(mockApiPatch).toHaveBeenCalledWith('/api/v1/experiments/exp-1', data)
        })
    })

    describe('archive', () => {
        it('calls POST /api/v1/experiments/{id}/archive with project_id', async () => {
            mockApiPost.mockResolvedValueOnce({ id: 'exp-1', archived: true })
            await experimentsApi.archive('exp-1', { project_id: 'p1' })
            expect(mockApiPost).toHaveBeenCalledWith(
                '/api/v1/experiments/exp-1/archive',
                {},
                { params: { project_id: 'p1' } },
            )
        })
    })

    describe('delete', () => {
        it('calls DELETE /api/v1/experiments/{id}', async () => {
            mockApiDelete.mockResolvedValueOnce(undefined as any)
            await experimentsApi.delete('exp-1')
            expect(mockApiDelete).toHaveBeenCalledWith('/api/v1/experiments/exp-1')
        })
    })

    describe('search', () => {
        it('calls GET /api/v1/experiments/search with query', async () => {
            mockApiGet.mockResolvedValueOnce({ experiments: [], total: 0 })
            await experimentsApi.search({ q: 'foo', project_id: 'p1', page: 1, page_size: 20 })
            expect(mockApiGet).toHaveBeenCalledWith('/api/v1/experiments/search', {
                params: { q: 'foo', project_id: 'p1', page: 1, page_size: 20 },
            })
        })
    })

    describe('exportData', () => {
        it('calls apiClient.get with responseType text and active project_id', async () => {
            mockGetActiveProjectId.mockReturnValue('active-proj')
            mockClientGet.mockResolvedValueOnce({ data: 'csv,data\n1,2' } as any)

            const result = await experimentsApi.exportData({ format: 'csv', status: 'completed' })

            expect(mockClientGet).toHaveBeenCalledWith('/api/v1/experiments/export', {
                params: { format: 'csv', status: 'completed', project_id: 'active-proj' },
                responseType: 'text',
            })
            expect(result).toBe('csv,data\n1,2')
        })

        it('uses provided project_id over active project', async () => {
            mockGetActiveProjectId.mockReturnValue('active-proj')
            mockClientGet.mockResolvedValueOnce({ data: 'json data' } as any)

            await experimentsApi.exportData({ project_id: 'override-proj', format: 'json' })

            expect(mockClientGet).toHaveBeenCalledWith('/api/v1/experiments/export', {
                params: { project_id: 'override-proj', format: 'json' },
                responseType: 'text',
            })
        })
    })
})
