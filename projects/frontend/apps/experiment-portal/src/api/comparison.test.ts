import { describe, it, expect, vi, beforeEach } from 'vitest'

vi.mock('./client', () => ({
    apiPost: vi.fn(),
}))

import { comparisonApi } from './comparison'
import { apiPost } from './client'

const mockApiPost = vi.mocked(apiPost)

describe('comparisonApi.compare', () => {
    beforeEach(() => {
        vi.clearAllMocks()
    })

    it('calls POST /api/v1/experiments/{id}/compare with run_ids and metric_names', async () => {
        const mockResponse = { runs: [{ id: 'r1' }] }
        mockApiPost.mockResolvedValueOnce(mockResponse)

        const result = await comparisonApi.compare('exp-1', {
            run_ids: ['r1', 'r2'],
            metric_names: ['loss', 'accuracy'],
        })

        expect(mockApiPost).toHaveBeenCalledWith('/api/v1/experiments/exp-1/compare', {
            run_ids: ['r1', 'r2'],
            metric_names: ['loss', 'accuracy'],
        })
        expect(result).toEqual(mockResponse)
    })

    it('propagates errors', async () => {
        mockApiPost.mockRejectedValueOnce(new Error('not found'))
        await expect(
            comparisonApi.compare('exp-1', { run_ids: [], metric_names: [] }),
        ).rejects.toThrow('not found')
    })
})

describe('comparisonApi.exportUrl', () => {
    it('builds CSV export URL with comma-separated ids and names', () => {
        const url = comparisonApi.exportUrl('exp-1', {
            run_ids: ['r1', 'r2', 'r3'],
            metric_names: ['loss', 'accuracy'],
            format: 'csv',
        })
        expect(url).toContain('/api/v1/experiments/exp-1/compare/export')
        expect(url).toContain('run_ids=r1%2Cr2%2Cr3')
        expect(url).toContain('names=loss%2Caccuracy')
        expect(url).toContain('format=csv')
    })

    it('builds JSON export URL', () => {
        const url = comparisonApi.exportUrl('exp-1', {
            run_ids: ['r1'],
            metric_names: ['loss'],
            format: 'json',
        })
        expect(url).toContain('format=json')
    })

    it('handles single run/metric correctly', () => {
        const url = comparisonApi.exportUrl('exp-2', {
            run_ids: ['r1'],
            metric_names: ['loss'],
            format: 'csv',
        })
        expect(url).toContain('run_ids=r1')
        expect(url).toContain('names=loss')
        expect(url).not.toContain('%2C')
    })

    it('handles empty arrays', () => {
        const url = comparisonApi.exportUrl('exp-1', {
            run_ids: [],
            metric_names: [],
            format: 'csv',
        })
        expect(url).toContain('run_ids=')
        expect(url).toContain('names=')
    })
})
