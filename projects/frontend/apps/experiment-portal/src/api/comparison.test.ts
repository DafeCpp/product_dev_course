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

import { comparisonApi } from './comparison'
import { setActiveProjectId } from '../utils/activeProject'

describe('comparisonApi', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        setActiveProjectId('project-1')
    })

    describe('compare', () => {
        it('POSTs to /api/v1/experiments/{id}/compare with run_ids and metric_names', async () => {
            const body = { run_ids: ['r1', 'r2'], metric_names: ['accuracy', 'loss'] }
            const response = { runs: [], summary: {} }
            mockAxiosInstance.post.mockResolvedValueOnce({ data: response })

            const result = await comparisonApi.compare('exp-1', body)

            expect(mockAxiosInstance.post).toHaveBeenCalledWith(
                '/api/v1/experiments/exp-1/compare',
                body,
                expect.objectContaining({
                    params: expect.objectContaining({ project_id: 'project-1' }),
                })
            )
            expect(result).toEqual(response)
        })

        it('propagates errors from the API', async () => {
            mockAxiosInstance.post.mockRejectedValueOnce(new Error('500 error'))
            await expect(
                comparisonApi.compare('exp-1', { run_ids: [], metric_names: [] })
            ).rejects.toThrow('500 error')
        })
    })

    describe('exportUrl', () => {
        it('builds /compare/export URL with comma-separated ids and names', () => {
            const url = comparisonApi.exportUrl('exp-1', {
                run_ids: ['r1', 'r2'],
                metric_names: ['accuracy', 'loss'],
                format: 'csv',
            })

            expect(url).toBe(
                '/api/v1/experiments/exp-1/compare/export?run_ids=r1%2Cr2&names=accuracy%2Closs&format=csv'
            )
        })

        it('honors json format', () => {
            const url = comparisonApi.exportUrl('exp-1', {
                run_ids: ['r1'],
                metric_names: ['m1'],
                format: 'json',
            })
            expect(url).toContain('format=json')
        })

        it('returns base path with empty arrays', () => {
            const url = comparisonApi.exportUrl('exp-1', {
                run_ids: [],
                metric_names: [],
                format: 'csv',
            })
            expect(url).toBe(
                '/api/v1/experiments/exp-1/compare/export?run_ids=&names=&format=csv'
            )
        })
    })
})
