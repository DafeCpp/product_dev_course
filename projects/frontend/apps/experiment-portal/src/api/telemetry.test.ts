import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'

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

import {
    captureSessionsApi,
    telemetryExportApi,
    telemetryApi,
    runEventsApi,
    captureSessionEventsApi,
} from './telemetry'
import { setActiveProjectId } from '../utils/activeProject'

const PROJECT_ID = 'project-1'

describe('captureSessionsApi', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        setActiveProjectId(PROJECT_ID)
    })

    it('list calls GET /api/v1/runs/{id}/capture-sessions', async () => {
        mockAxiosInstance.get.mockResolvedValueOnce({ data: { sessions: [], total: 0 } })

        await captureSessionsApi.list('run-1', { page: 2, page_size: 10 })

        expect(mockAxiosInstance.get).toHaveBeenCalledWith(
            '/api/v1/runs/run-1/capture-sessions',
            expect.objectContaining({
                params: expect.objectContaining({ page: 2, page_size: 10, project_id: PROJECT_ID }),
            })
        )
    })

    it('get calls GET /api/v1/runs/{rid}/capture-sessions/{sid}', async () => {
        mockAxiosInstance.get.mockResolvedValueOnce({ data: { id: 'sess-1' } })

        await captureSessionsApi.get('run-1', 'sess-1')

        expect(mockAxiosInstance.get).toHaveBeenCalledWith(
            '/api/v1/runs/run-1/capture-sessions/sess-1',
            expect.objectContaining({
                params: expect.objectContaining({ project_id: PROJECT_ID }),
            })
        )
    })

    it('create POSTs the payload and forwards explicit project_id param', async () => {
        const payload = { name: 'Capture' }
        mockAxiosInstance.post.mockResolvedValueOnce({ data: { id: 'sess-1', ...payload } })

        await captureSessionsApi.create('run-1', payload as any, { project_id: 'override-project' })

        expect(mockAxiosInstance.post).toHaveBeenCalledWith(
            '/api/v1/runs/run-1/capture-sessions',
            payload,
            expect.objectContaining({
                params: expect.objectContaining({ project_id: 'override-project' }),
            })
        )
    })

    it('stop POSTs to /capture-sessions/{sid}/stop', async () => {
        mockAxiosInstance.post.mockResolvedValueOnce({ data: { id: 'sess-1', status: 'stopped' } })

        await captureSessionsApi.stop('run-1', 'sess-1')

        expect(mockAxiosInstance.post).toHaveBeenCalledWith(
            '/api/v1/runs/run-1/capture-sessions/sess-1/stop',
            {},
            expect.any(Object)
        )
    })

    it('delete DELETEs /capture-sessions/{sid}', async () => {
        mockAxiosInstance.delete.mockResolvedValueOnce({ data: {} })

        await captureSessionsApi.delete('run-1', 'sess-1')

        expect(mockAxiosInstance.delete).toHaveBeenCalledWith(
            '/api/v1/runs/run-1/capture-sessions/sess-1',
            expect.objectContaining({
                params: expect.objectContaining({ project_id: PROJECT_ID }),
            })
        )
    })

    it('startBackfill POSTs to /backfill/start', async () => {
        mockAxiosInstance.post.mockResolvedValueOnce({ data: { id: 'sess-1' } })

        await captureSessionsApi.startBackfill('run-1', 'sess-1')

        expect(mockAxiosInstance.post).toHaveBeenCalledWith(
            '/api/v1/runs/run-1/capture-sessions/sess-1/backfill/start',
            undefined,
            expect.any(Object)
        )
    })

    it('completeBackfill POSTs to /backfill/complete', async () => {
        mockAxiosInstance.post.mockResolvedValueOnce({
            data: { id: 'sess-1', attached_records: 42 },
        })

        const result = await captureSessionsApi.completeBackfill('run-1', 'sess-1')

        expect(mockAxiosInstance.post).toHaveBeenCalledWith(
            '/api/v1/runs/run-1/capture-sessions/sess-1/backfill/complete',
            undefined,
            expect.any(Object)
        )
        expect(result.attached_records).toBe(42)
    })
})

describe('telemetryExportApi', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        setActiveProjectId(PROJECT_ID)
    })

    it('exportSession calls /capture-sessions/{sid}/telemetry/export with text responseType', async () => {
        mockAxiosInstance.get.mockResolvedValueOnce({ data: 'csv,data' })

        const result = await telemetryExportApi.exportSession('run-1', 'sess-1', {
            format: 'csv',
            sensor_id: 'sensor-1',
            include_late: true,
        })

        expect(mockAxiosInstance.get).toHaveBeenCalledWith(
            '/api/v1/runs/run-1/capture-sessions/sess-1/telemetry/export',
            expect.objectContaining({
                params: expect.objectContaining({
                    format: 'csv',
                    sensor_id: 'sensor-1',
                    include_late: true,
                    project_id: PROJECT_ID,
                }),
                responseType: 'text',
            })
        )
        expect(result).toBe('csv,data')
    })

    it('exportRun calls /runs/{rid}/telemetry/export with text responseType', async () => {
        mockAxiosInstance.get.mockResolvedValueOnce({ data: 'csv,data' })

        const result = await telemetryExportApi.exportRun('run-1', { format: 'json' })

        expect(mockAxiosInstance.get).toHaveBeenCalledWith(
            '/api/v1/runs/run-1/telemetry/export',
            expect.objectContaining({
                params: expect.objectContaining({ format: 'json', project_id: PROJECT_ID }),
                responseType: 'text',
            })
        )
        expect(result).toBe('csv,data')
    })
})

describe('telemetryApi.ingest', () => {
    beforeEach(() => {
        vi.clearAllMocks()
    })

    it('POSTs to /api/v1/telemetry with sensor token and skip-auth flag', async () => {
        mockAxiosInstance.post.mockResolvedValueOnce({ data: { accepted: 1 } })

        const data = { sensor_id: 's1', readings: [] }
        const result = await telemetryApi.ingest(data as any, 'sensor-token-xyz')

        expect(mockAxiosInstance.post).toHaveBeenCalledWith(
            '/api/v1/telemetry',
            data,
            expect.objectContaining({
                headers: expect.objectContaining({
                    Authorization: 'Bearer sensor-token-xyz',
                    'Content-Type': 'application/json',
                }),
                _skipAuthInterceptor: true,
            })
        )
        expect(result).toEqual({ accepted: 1 })
    })
})

describe('telemetryApi.query / aggregated', () => {
    let fetchMock: ReturnType<typeof vi.fn>

    beforeEach(() => {
        fetchMock = vi.fn()
        vi.stubGlobal('fetch', fetchMock)
        setActiveProjectId(PROJECT_ID)
    })

    afterEach(() => {
        vi.unstubAllGlobals()
    })

    it('query builds URL with capture_session_id and repeated sensor_id params', async () => {
        fetchMock.mockResolvedValueOnce(
            new Response(JSON.stringify({ readings: [] }), { status: 200 })
        )

        await telemetryApi.query({
            capture_session_id: 'sess-1',
            sensor_id: ['s1', 's2'],
            since_id: 100,
            limit: 50,
            include_late: true,
            order: 'desc',
        })

        const calledUrl = String(fetchMock.mock.calls[0][0])
        expect(calledUrl).toContain('/api/v1/telemetry/query')
        expect(calledUrl).toContain('capture_session_id=sess-1')
        expect(calledUrl).toContain('sensor_id=s1')
        expect(calledUrl).toContain('sensor_id=s2')
        expect(calledUrl).toContain('since_id=100')
        expect(calledUrl).toContain('limit=50')
        expect(calledUrl).toContain('include_late=true')
        expect(calledUrl).toContain('order=desc')
    })

    it('query throws on non-2xx response', async () => {
        fetchMock.mockResolvedValueOnce(
            new Response('boom', { status: 500, statusText: 'Server Error' })
        )

        await expect(
            telemetryApi.query({ capture_session_id: 'sess-1' })
        ).rejects.toThrow(/boom|HTTP 500/)
    })

    it('aggregated builds URL with signal/time_from/time_to', async () => {
        fetchMock.mockResolvedValueOnce(
            new Response(JSON.stringify({ buckets: [] }), { status: 200 })
        )

        await telemetryApi.aggregated({
            capture_session_id: 'sess-1',
            sensor_id: ['s1'],
            signal: 'temperature',
            time_from: '2025-01-01T00:00:00Z',
            time_to: '2025-01-02T00:00:00Z',
            limit: 100,
            order: 'asc',
        })

        const calledUrl = String(fetchMock.mock.calls[0][0])
        expect(calledUrl).toContain('/api/v1/telemetry/aggregated')
        expect(calledUrl).toContain('signal=temperature')
        expect(calledUrl).toContain('time_from=2025-01-01T00%3A00%3A00Z')
        expect(calledUrl).toContain('time_to=2025-01-02T00%3A00%3A00Z')
        expect(calledUrl).toContain('order=asc')
    })
})

describe('runEventsApi', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        setActiveProjectId(PROJECT_ID)
    })

    it('list calls GET /api/v1/runs/{id}/events', async () => {
        mockAxiosInstance.get.mockResolvedValueOnce({ data: { events: [], total: 0 } })

        await runEventsApi.list('run-1', { page: 1, page_size: 25 })

        expect(mockAxiosInstance.get).toHaveBeenCalledWith(
            '/api/v1/runs/run-1/events',
            expect.objectContaining({
                params: expect.objectContaining({
                    page: 1,
                    page_size: 25,
                    project_id: PROJECT_ID,
                }),
            })
        )
    })
})

describe('captureSessionEventsApi', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        setActiveProjectId(PROJECT_ID)
    })

    it('list calls GET /api/v1/runs/{rid}/capture-sessions/{sid}/events', async () => {
        mockAxiosInstance.get.mockResolvedValueOnce({ data: { events: [], total: 0 } })

        await captureSessionEventsApi.list('run-1', 'sess-1', { page: 1, page_size: 10 })

        expect(mockAxiosInstance.get).toHaveBeenCalledWith(
            '/api/v1/runs/run-1/capture-sessions/sess-1/events',
            expect.objectContaining({
                params: expect.objectContaining({ project_id: PROJECT_ID }),
            })
        )
    })
})
