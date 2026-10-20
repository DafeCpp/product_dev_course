import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'

const { apiClientGet, apiFetchMock, getActiveProjectIdMock, getCsrfTokenMock } = vi.hoisted(() => ({
    apiClientGet: vi.fn(),
    apiFetchMock: vi.fn(),
    getActiveProjectIdMock: vi.fn<() => string | null>(),
    getCsrfTokenMock: vi.fn<() => string | undefined>(),
}))

vi.mock('./client', () => ({
    apiGet: vi.fn(),
    apiPost: vi.fn(),
    apiDelete: vi.fn(),
    apiClient: { get: apiClientGet, post: vi.fn() },
}))

vi.mock('./http/apiFetch', () => ({
    apiFetch: apiFetchMock,
    makeFetchHeaders: () => ({ 'X-Trace-Id': 't', 'X-Request-Id': 'r' }),
}))

vi.mock('../utils/activeProject', () => ({
    getActiveProjectId: getActiveProjectIdMock,
}))

vi.mock('../utils/csrf', () => ({
    getCsrfToken: getCsrfTokenMock,
}))

import { telemetryApi, telemetryExportApi } from './telemetry'

beforeEach(() => {
    vi.clearAllMocks()
    getActiveProjectIdMock.mockReturnValue(null)
    getCsrfTokenMock.mockReturnValue(undefined)
})

describe('telemetryExportApi without an active project', () => {
    it('exportSession sends project_id undefined', async () => {
        apiClientGet.mockResolvedValueOnce({ data: 'csv' })
        await expect(telemetryExportApi.exportSession('r1', 's1')).resolves.toBe('csv')
        expect(apiClientGet.mock.calls[0][1].params).toEqual({ project_id: undefined })
    })

    it('exportRun sends project_id undefined and keeps other params', async () => {
        apiClientGet.mockResolvedValueOnce({ data: 'json' })
        await expect(telemetryExportApi.exportRun('r1', { format: 'json' })).resolves.toBe('json')
        expect(apiClientGet.mock.calls[0][1].params).toEqual({ format: 'json', project_id: undefined })
    })
})

describe('telemetryApi.query / aggregated optional params', () => {
    it('query with only capture_session_id adds no optional params', async () => {
        apiFetchMock.mockResolvedValueOnce({ readings: [] })
        await telemetryApi.query({ capture_session_id: 'c1' })
        const url = new URL(apiFetchMock.mock.calls[0][0])
        expect([...url.searchParams.keys()]).toEqual(['capture_session_id'])
    })

    it('query skips empty sensor ids, writes include_late=false, ignores unknown order', async () => {
        apiFetchMock.mockResolvedValueOnce({ readings: [] })
        await telemetryApi.query({
            capture_session_id: 'c1',
            sensor_id: ['', 's2'],
            include_late: false,
            order: 'random' as unknown as 'asc',
        })
        const url = new URL(apiFetchMock.mock.calls[0][0])
        expect(url.searchParams.getAll('sensor_id')).toEqual(['s2'])
        expect(url.searchParams.get('include_late')).toBe('false')
        expect(url.searchParams.has('order')).toBe(false)
    })

    it('aggregated with only capture_session_id adds no optional params', async () => {
        apiFetchMock.mockResolvedValueOnce({ buckets: [] })
        await telemetryApi.aggregated({ capture_session_id: 'c1' })
        const url = new URL(apiFetchMock.mock.calls[0][0])
        expect([...url.searchParams.keys()]).toEqual(['capture_session_id'])
    })

    it('aggregated skips empty sensor ids and writes desc order', async () => {
        apiFetchMock.mockResolvedValueOnce({ buckets: [] })
        await telemetryApi.aggregated({ capture_session_id: 'c1', sensor_id: ['', 'a'], order: 'desc' })
        const url = new URL(apiFetchMock.mock.calls[0][0])
        expect(url.searchParams.getAll('sensor_id')).toEqual(['a'])
        expect(url.searchParams.get('order')).toBe('desc')
    })
})

describe('telemetryApi.stream branches', () => {
    let fetchMock: ReturnType<typeof vi.fn>

    beforeEach(() => {
        fetchMock = vi.fn()
        vi.stubGlobal('fetch', fetchMock)
    })

    afterEach(() => {
        vi.unstubAllGlobals()
    })

    it('omits project_id and empty since_ts, adds max_events', async () => {
        fetchMock.mockResolvedValueOnce(new Response(null, { status: 200 }))
        const { response, debug } = await telemetryApi.stream({ sensor_id: 's1', since_ts: '', max_events: 5 })
        expect(response.status).toBe(200)
        const url = new URL(debug.url)
        expect(url.searchParams.has('project_id')).toBe(false)
        expect(url.searchParams.has('since_ts')).toBe(false)
        expect(url.searchParams.get('max_events')).toBe('5')
        expect(debug.method).toBe('GET')
    })

    it('adds project_id when an active project is set', async () => {
        getActiveProjectIdMock.mockReturnValue('p1')
        fetchMock.mockResolvedValueOnce(new Response(null, { status: 200 }))
        const { debug } = await telemetryApi.stream({ sensor_id: 's1' })
        expect(new URL(debug.url).searchParams.get('project_id')).toBe('p1')
    })

    it('returns the original 401 response when refresh fails (no CSRF header)', async () => {
        fetchMock
            .mockResolvedValueOnce(new Response(null, { status: 401 }))
            .mockResolvedValueOnce(new Response(null, { status: 401 }))
        const { response } = await telemetryApi.stream({ sensor_id: 's1' })
        expect(response.status).toBe(401)
        expect(fetchMock).toHaveBeenCalledTimes(2)
        const refreshInit = fetchMock.mock.calls[1][1] as RequestInit
        expect(String(fetchMock.mock.calls[1][0])).toContain('/auth/refresh')
        expect(refreshInit.headers).not.toHaveProperty('X-CSRF-Token')
    })

    it('sends the CSRF token on refresh when present', async () => {
        getCsrfTokenMock.mockReturnValue('csrf-1')
        fetchMock
            .mockResolvedValueOnce(new Response(null, { status: 401 }))
            .mockResolvedValueOnce(new Response(null, { status: 200 }))
            .mockResolvedValueOnce(new Response(null, { status: 200 }))
        const { response } = await telemetryApi.stream({ sensor_id: 's1' })
        expect(response.status).toBe(200)
        expect((fetchMock.mock.calls[1][1] as RequestInit).headers).toHaveProperty('X-CSRF-Token', 'csrf-1')
    })

    it('attaches debug info to a thrown fetch error', async () => {
        const err = new Error('network down')
        fetchMock.mockRejectedValueOnce(err)
        await expect(telemetryApi.stream({ sensor_id: 's1' })).rejects.toBe(err)
        expect((err as unknown as { debug: { method: string } }).debug.method).toBe('GET')
    })
})
