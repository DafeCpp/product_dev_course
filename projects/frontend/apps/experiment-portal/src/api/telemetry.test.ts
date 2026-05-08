import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'

vi.mock('./client', () => ({
    apiGet: vi.fn(),
    apiPost: vi.fn(),
    apiDelete: vi.fn(),
    apiClient: {
        get: vi.fn(),
        post: vi.fn(),
    },
}))

vi.mock('./http/baseUrl', () => ({
    AUTH_PROXY_URL: 'http://proxy',
    TELEMETRY_BASE_URL: 'http://telemetry',
}))

vi.mock('./http/apiFetch', () => ({
    apiFetch: vi.fn(),
    makeFetchHeaders: vi.fn(() => ({ 'X-Trace-Id': 'trace-1', 'X-Request-Id': 'req-1' })),
}))

vi.mock('../utils/activeProject', () => ({
    getActiveProjectId: vi.fn(() => null),
}))

vi.mock('../utils/csrf', () => ({
    getCsrfToken: vi.fn(() => 'csrf-1'),
}))

import {
    captureSessionsApi,
    telemetryExportApi,
    telemetryApi,
    runEventsApi,
    captureSessionEventsApi,
} from './telemetry'
import { apiGet, apiPost, apiDelete, apiClient } from './client'
import { apiFetch } from './http/apiFetch'
import { getActiveProjectId } from '../utils/activeProject'

const mockApiGet = vi.mocked(apiGet)
const mockApiPost = vi.mocked(apiPost)
const mockApiDelete = vi.mocked(apiDelete)
const mockClientGet = vi.mocked(apiClient.get)
const mockClientPost = vi.mocked(apiClient.post)
const mockApiFetch = vi.mocked(apiFetch)
const mockGetActiveProjectId = vi.mocked(getActiveProjectId)

describe('captureSessionsApi', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        mockGetActiveProjectId.mockReturnValue(null)
    })

    it('list calls GET /api/v1/runs/{id}/capture-sessions', async () => {
        mockApiGet.mockResolvedValueOnce({ sessions: [], total: 0 })
        await captureSessionsApi.list('run-1', { page: 1, page_size: 20 })
        expect(mockApiGet).toHaveBeenCalledWith('/api/v1/runs/run-1/capture-sessions', {
            params: { page: 1, page_size: 20 },
        })
    })

    it('get calls GET /api/v1/runs/{id}/capture-sessions/{sid}', async () => {
        mockApiGet.mockResolvedValueOnce({ id: 'cs-1' })
        await captureSessionsApi.get('run-1', 'cs-1')
        expect(mockApiGet).toHaveBeenCalledWith('/api/v1/runs/run-1/capture-sessions/cs-1')
    })

    it('create calls POST with project_id param', async () => {
        mockApiPost.mockResolvedValueOnce({ id: 'cs-1' })
        await captureSessionsApi.create('run-1', { name: 'Session A' } as any, { project_id: 'p1' })
        expect(mockApiPost).toHaveBeenCalledWith(
            '/api/v1/runs/run-1/capture-sessions',
            { name: 'Session A' },
            { params: { project_id: 'p1' } },
        )
    })

    it('stop calls POST /api/v1/runs/{id}/capture-sessions/{sid}/stop', async () => {
        mockApiPost.mockResolvedValueOnce({ id: 'cs-1', status: 'stopped' })
        await captureSessionsApi.stop('run-1', 'cs-1')
        expect(mockApiPost).toHaveBeenCalledWith('/api/v1/runs/run-1/capture-sessions/cs-1/stop', {})
    })

    it('delete calls DELETE', async () => {
        mockApiDelete.mockResolvedValueOnce(undefined as any)
        await captureSessionsApi.delete('run-1', 'cs-1')
        expect(mockApiDelete).toHaveBeenCalledWith('/api/v1/runs/run-1/capture-sessions/cs-1')
    })

    it('startBackfill calls POST /backfill/start', async () => {
        mockApiPost.mockResolvedValueOnce({ id: 'cs-1' })
        await captureSessionsApi.startBackfill('run-1', 'cs-1')
        expect(mockApiPost).toHaveBeenCalledWith(
            '/api/v1/runs/run-1/capture-sessions/cs-1/backfill/start',
        )
    })

    it('completeBackfill calls POST /backfill/complete', async () => {
        mockApiPost.mockResolvedValueOnce({ id: 'cs-1', attached_records: 100 })
        await captureSessionsApi.completeBackfill('run-1', 'cs-1')
        expect(mockApiPost).toHaveBeenCalledWith(
            '/api/v1/runs/run-1/capture-sessions/cs-1/backfill/complete',
        )
    })
})

describe('telemetryExportApi', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        mockGetActiveProjectId.mockReturnValue('active-proj')
    })

    it('exportSession calls apiClient.get with params and responseType text', async () => {
        mockClientGet.mockResolvedValueOnce({ data: 'csv data' } as any)

        const result = await telemetryExportApi.exportSession('run-1', 'cs-1', {
            format: 'csv',
            sensor_id: 's1',
            include_late: true,
            raw_or_physical: 'physical',
        })

        expect(mockClientGet).toHaveBeenCalledWith(
            '/api/v1/runs/run-1/capture-sessions/cs-1/telemetry/export',
            {
                params: {
                    format: 'csv',
                    sensor_id: 's1',
                    include_late: true,
                    raw_or_physical: 'physical',
                    project_id: 'active-proj',
                },
                responseType: 'text',
            },
        )
        expect(result).toBe('csv data')
    })

    it('exportRun calls apiClient.get with run-level params', async () => {
        mockClientGet.mockResolvedValueOnce({ data: 'json data' } as any)

        await telemetryExportApi.exportRun('run-1', { format: 'json', signal: 'voltage' })

        expect(mockClientGet).toHaveBeenCalledWith('/api/v1/runs/run-1/telemetry/export', {
            params: { format: 'json', signal: 'voltage', project_id: 'active-proj' },
            responseType: 'text',
        })
    })
})

describe('telemetryApi.ingest', () => {
    beforeEach(() => {
        vi.clearAllMocks()
    })

    it('calls apiClient.post with TELEMETRY_BASE_URL and Bearer token', async () => {
        mockClientPost.mockResolvedValueOnce({ data: { accepted: 1 } } as any)

        const result = await telemetryApi.ingest({ readings: [] } as any, 'sensor-token-1')

        expect(mockClientPost).toHaveBeenCalledWith(
            '/api/v1/telemetry',
            { readings: [] },
            expect.objectContaining({
                baseURL: 'http://telemetry',
                headers: expect.objectContaining({
                    Authorization: 'Bearer sensor-token-1',
                    'Content-Type': 'application/json',
                }),
                _skipAuthInterceptor: true,
            }),
        )
        expect(result).toEqual({ accepted: 1 })
    })
})

describe('telemetryApi.stream', () => {
    let fetchMock: ReturnType<typeof vi.fn>

    beforeEach(() => {
        vi.clearAllMocks()
        fetchMock = vi.fn()
        vi.stubGlobal('fetch', fetchMock)
    })

    afterEach(() => {
        vi.unstubAllGlobals()
    })

    it('builds URL with sensor_id and optional params and returns response+debug', async () => {
        const mockResponse = { status: 200, ok: true } as Response
        fetchMock.mockResolvedValueOnce(mockResponse)

        const result = await telemetryApi.stream({
            sensor_id: 's1',
            since_ts: '2025-01-01T00:00:00Z',
            since_id: 100,
            max_events: 500,
            idle_timeout_seconds: 30,
        })

        expect(fetchMock).toHaveBeenCalledTimes(1)
        const calledUrl = fetchMock.mock.calls[0][0] as string
        expect(calledUrl).toContain('http://proxy/api/v1/telemetry/stream')
        expect(calledUrl).toContain('sensor_id=s1')
        expect(calledUrl).toContain('since_ts=2025-01-01T00%3A00%3A00Z')
        expect(calledUrl).toContain('since_id=100')
        expect(calledUrl).toContain('max_events=500')
        expect(calledUrl).toContain('idle_timeout_seconds=30')

        expect(result.response).toBe(mockResponse)
        expect(result.debug.method).toBe('GET')
    })

    it('retries with refresh on 401 response', async () => {
        const unauth = { status: 401, ok: false } as Response
        const refreshOk = { ok: true } as Response
        const retried = { status: 200, ok: true } as Response
        fetchMock
            .mockResolvedValueOnce(unauth) // initial stream
            .mockResolvedValueOnce(refreshOk) // refresh
            .mockResolvedValueOnce(retried) // retry stream

        const result = await telemetryApi.stream({ sensor_id: 's1' })

        expect(fetchMock).toHaveBeenCalledTimes(3)
        const refreshUrl = fetchMock.mock.calls[1][0] as string
        expect(refreshUrl).toBe('http://proxy/auth/refresh')
        expect(result.response).toBe(retried)
    })

    it('throws when fetch rejects, attaching debug info', async () => {
        const err = Object.assign(new Error('network down'), {} as any)
        fetchMock.mockRejectedValueOnce(err)

        await expect(telemetryApi.stream({ sensor_id: 's1' })).rejects.toThrow('network down')
        expect((err as any).debug).toBeDefined()
        expect((err as any).debug.method).toBe('GET')
    })
})

describe('telemetryApi.query', () => {
    beforeEach(() => {
        vi.clearAllMocks()
    })

    it('builds URL with capture_session_id and optional sensor_id array', async () => {
        mockApiFetch.mockResolvedValueOnce({ telemetry: [] } as any)

        await telemetryApi.query({
            capture_session_id: 'cs-1',
            sensor_id: ['s1', 's2'],
            since_id: 42,
            limit: 100,
            include_late: true,
            order: 'desc',
        })

        const calledUrl = mockApiFetch.mock.calls[0][0]
        expect(calledUrl).toContain('http://telemetry/api/v1/telemetry/query')
        expect(calledUrl).toContain('capture_session_id=cs-1')
        expect(calledUrl).toContain('sensor_id=s1')
        expect(calledUrl).toContain('sensor_id=s2')
        expect(calledUrl).toContain('since_id=42')
        expect(calledUrl).toContain('limit=100')
        expect(calledUrl).toContain('include_late=true')
        expect(calledUrl).toContain('order=desc')
    })

    it('omits optional params when not provided', async () => {
        mockApiFetch.mockResolvedValueOnce({ telemetry: [] } as any)
        await telemetryApi.query({ capture_session_id: 'cs-1' })
        const calledUrl = mockApiFetch.mock.calls[0][0]
        expect(calledUrl).toContain('capture_session_id=cs-1')
        expect(calledUrl).not.toContain('sensor_id=')
        expect(calledUrl).not.toContain('since_id=')
    })
})

describe('telemetryApi.aggregated', () => {
    beforeEach(() => {
        vi.clearAllMocks()
    })

    it('builds URL with all aggregation params', async () => {
        mockApiFetch.mockResolvedValueOnce({ buckets: [] } as any)

        await telemetryApi.aggregated({
            capture_session_id: 'cs-1',
            sensor_id: ['s1'],
            signal: 'voltage',
            time_from: '2025-01-01T00:00:00Z',
            time_to: '2025-01-02T00:00:00Z',
            limit: 1000,
            order: 'asc',
        })

        const calledUrl = mockApiFetch.mock.calls[0][0]
        expect(calledUrl).toContain('http://telemetry/api/v1/telemetry/aggregated')
        expect(calledUrl).toContain('capture_session_id=cs-1')
        expect(calledUrl).toContain('sensor_id=s1')
        expect(calledUrl).toContain('signal=voltage')
        expect(calledUrl).toContain('time_from=2025-01-01T00%3A00%3A00Z')
        expect(calledUrl).toContain('time_to=2025-01-02T00%3A00%3A00Z')
        expect(calledUrl).toContain('limit=1000')
        expect(calledUrl).toContain('order=asc')
    })
})

describe('runEventsApi.list', () => {
    beforeEach(() => {
        vi.clearAllMocks()
    })

    it('calls GET /api/v1/runs/{id}/events with pagination', async () => {
        mockApiGet.mockResolvedValueOnce({ events: [], total: 0 })
        await runEventsApi.list('run-1', { page: 2, page_size: 50 })
        expect(mockApiGet).toHaveBeenCalledWith('/api/v1/runs/run-1/events', {
            params: { page: 2, page_size: 50 },
        })
    })
})

describe('captureSessionEventsApi.list', () => {
    beforeEach(() => {
        vi.clearAllMocks()
    })

    it('calls GET /api/v1/runs/{id}/capture-sessions/{sid}/events', async () => {
        mockApiGet.mockResolvedValueOnce({ events: [], total: 0 })
        await captureSessionEventsApi.list('run-1', 'cs-1')
        expect(mockApiGet).toHaveBeenCalledWith(
            '/api/v1/runs/run-1/capture-sessions/cs-1/events',
            { params: undefined },
        )
    })
})
