import { describe, it, expect, vi, beforeEach } from 'vitest'

vi.mock('./client', () => ({
    apiGet: vi.fn(),
    apiPost: vi.fn(),
}))

import { metricsApi } from './metrics'
import { apiGet, apiPost } from './client'

const mockApiGet = vi.mocked(apiGet)
const mockApiPost = vi.mocked(apiPost)

describe('metricsApi', () => {
    beforeEach(() => {
        vi.clearAllMocks()
    })

    describe('query', () => {
        it('calls GET /api/v1/runs/{id}/metrics with name and step range', async () => {
            mockApiGet.mockResolvedValueOnce({ series: {} })
            await metricsApi.query('run-1', { name: 'loss', from_step: 0, to_step: 100 })
            expect(mockApiGet).toHaveBeenCalledWith('/api/v1/runs/run-1/metrics', {
                params: { name: 'loss', from_step: 0, to_step: 100 },
            })
        })

        it('handles missing params', async () => {
            mockApiGet.mockResolvedValueOnce({ series: {} })
            await metricsApi.query('run-1')
            expect(mockApiGet).toHaveBeenCalledWith('/api/v1/runs/run-1/metrics', { params: undefined })
        })
    })

    describe('record', () => {
        it('calls POST /api/v1/runs/{id}/metrics with batch metrics', async () => {
            const metrics = [
                { name: 'loss', step: 1, value: 0.5 },
                { name: 'accuracy', step: 1, value: 0.9 },
            ]
            mockApiPost.mockResolvedValueOnce({ accepted: 2 })

            const result = await metricsApi.record('run-1', metrics)

            expect(mockApiPost).toHaveBeenCalledWith('/api/v1/runs/run-1/metrics', { metrics })
            expect(result).toEqual({ accepted: 2 })
        })

        it('propagates errors', async () => {
            mockApiPost.mockRejectedValueOnce(new Error('invalid metric'))
            await expect(metricsApi.record('run-1', [])).rejects.toThrow('invalid metric')
        })
    })

    describe('list', () => {
        it('calls GET /api/v1/runs/{id}/metrics with names filter and ordering', async () => {
            mockApiGet.mockResolvedValueOnce({ items: [], total: 0 })
            await metricsApi.list('run-1', {
                names: 'loss,accuracy',
                from_step: 0,
                to_step: 100,
                order: 'asc',
                limit: 1000,
                offset: 0,
            })
            expect(mockApiGet).toHaveBeenCalledWith('/api/v1/runs/run-1/metrics', {
                params: {
                    names: 'loss,accuracy',
                    from_step: 0,
                    to_step: 100,
                    order: 'asc',
                    limit: 1000,
                    offset: 0,
                },
            })
        })
    })

    describe('summary', () => {
        it('calls GET /api/v1/runs/{id}/metrics/summary with names', async () => {
            mockApiGet.mockResolvedValueOnce({ summaries: [] })
            await metricsApi.summary('run-1', 'loss,accuracy')
            expect(mockApiGet).toHaveBeenCalledWith('/api/v1/runs/run-1/metrics/summary', {
                params: { names: 'loss,accuracy' },
            })
        })

        it('passes undefined params when names omitted', async () => {
            mockApiGet.mockResolvedValueOnce({ summaries: [] })
            await metricsApi.summary('run-1')
            expect(mockApiGet).toHaveBeenCalledWith('/api/v1/runs/run-1/metrics/summary', {
                params: undefined,
            })
        })
    })

    describe('aggregations', () => {
        it('calls GET /api/v1/runs/{id}/metrics/aggregations with bucket size', async () => {
            mockApiGet.mockResolvedValueOnce({ aggregations: [] })
            await metricsApi.aggregations('run-1', {
                names: 'loss',
                from_step: 0,
                to_step: 10000,
                bucket_size: 100,
            })
            expect(mockApiGet).toHaveBeenCalledWith('/api/v1/runs/run-1/metrics/aggregations', {
                params: { names: 'loss', from_step: 0, to_step: 10000, bucket_size: 100 },
            })
        })
    })
})
