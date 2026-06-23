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

import { metricsApi } from './metrics'
import { setActiveProjectId } from '../utils/activeProject'

describe('metricsApi', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        setActiveProjectId('project-1')
    })

    describe('query (legacy)', () => {
        it('GETs /api/v1/runs/{id}/metrics with name and step range', async () => {
            mockAxiosInstance.get.mockResolvedValueOnce({ data: { series: {} } })

            await metricsApi.query('run-1', { name: 'accuracy', from_step: 0, to_step: 100 })

            expect(mockAxiosInstance.get).toHaveBeenCalledWith(
                '/api/v1/runs/run-1/metrics',
                expect.objectContaining({
                    params: expect.objectContaining({
                        name: 'accuracy',
                        from_step: 0,
                        to_step: 100,
                        project_id: 'project-1',
                    }),
                })
            )
        })
    })

    describe('record', () => {
        it('POSTs metrics array to /api/v1/runs/{id}/metrics', async () => {
            const metrics = [
                { name: 'accuracy', step: 1, value: 0.5 },
                { name: 'accuracy', step: 2, value: 0.6 },
            ]
            mockAxiosInstance.post.mockResolvedValueOnce({ data: { accepted: 2 } })

            const result = await metricsApi.record('run-1', metrics)

            expect(mockAxiosInstance.post).toHaveBeenCalledWith(
                '/api/v1/runs/run-1/metrics',
                { metrics },
                expect.any(Object)
            )
            expect(result).toEqual({ accepted: 2 })
        })
    })

    describe('list', () => {
        it('GETs /api/v1/runs/{id}/metrics with names/order/limit/offset', async () => {
            mockAxiosInstance.get.mockResolvedValueOnce({ data: { metrics: [], total: 0 } })

            await metricsApi.list('run-1', {
                names: 'accuracy,loss',
                from_step: 0,
                to_step: 1000,
                order: 'step.asc',
                limit: 100,
                offset: 0,
            })

            expect(mockAxiosInstance.get).toHaveBeenCalledWith(
                '/api/v1/runs/run-1/metrics',
                expect.objectContaining({
                    params: expect.objectContaining({
                        names: 'accuracy,loss',
                        order: 'step.asc',
                        limit: 100,
                        offset: 0,
                    }),
                })
            )
        })
    })

    describe('summary', () => {
        it('GETs /metrics/summary with names param', async () => {
            mockAxiosInstance.get.mockResolvedValueOnce({ data: { summaries: {} } })

            await metricsApi.summary('run-1', 'accuracy,loss')

            expect(mockAxiosInstance.get).toHaveBeenCalledWith(
                '/api/v1/runs/run-1/metrics/summary',
                expect.objectContaining({
                    params: expect.objectContaining({
                        names: 'accuracy,loss',
                        project_id: 'project-1',
                    }),
                })
            )
        })

        it('GETs /metrics/summary without names when omitted (project_id still injected)', async () => {
            mockAxiosInstance.get.mockResolvedValueOnce({ data: { summaries: {} } })

            await metricsApi.summary('run-1')

            expect(mockAxiosInstance.get).toHaveBeenCalledWith(
                '/api/v1/runs/run-1/metrics/summary',
                expect.objectContaining({
                    params: expect.objectContaining({ project_id: 'project-1' }),
                })
            )
        })
    })

    describe('aggregations', () => {
        it('GETs /metrics/aggregations with names + bucket_size', async () => {
            mockAxiosInstance.get.mockResolvedValueOnce({ data: { buckets: [] } })

            await metricsApi.aggregations('run-1', {
                names: 'accuracy',
                from_step: 0,
                to_step: 10000,
                bucket_size: 100,
            })

            expect(mockAxiosInstance.get).toHaveBeenCalledWith(
                '/api/v1/runs/run-1/metrics/aggregations',
                expect.objectContaining({
                    params: expect.objectContaining({
                        names: 'accuracy',
                        from_step: 0,
                        to_step: 10000,
                        bucket_size: 100,
                    }),
                })
            )
        })
    })
})
