/** Telemetry, Capture Sessions, and related Events API */
import type { AxiosRequestConfig } from 'axios'
import type {
  CaptureSession,
  CaptureSessionCreate,
  CaptureSessionsListResponse,
  TelemetryIngest,
  TelemetryIngestResponse,
  TelemetryQueryResponse,
  TelemetryAggregatedResponse,
  RunEventsListResponse,
  CaptureSessionEventsListResponse,
} from '../types'
import { getCsrfToken } from '../utils/csrf'
import { getActiveProjectId } from '../utils/activeProject'
import { apiGet, apiPost, apiDelete, apiClient } from './client'
import { AUTH_PROXY_URL, TELEMETRY_BASE_URL } from './http/baseUrl'
import { apiFetch, makeFetchHeaders } from './http/apiFetch'

// Capture Sessions API
export const captureSessionsApi = {
  list: async (runId: string, params?: {
    page?: number
    page_size?: number
  }): Promise<CaptureSessionsListResponse> => {
    return await apiGet(`/api/v1/runs/${runId}/capture-sessions`, { params })
  },

  get: async (runId: string, sessionId: string): Promise<CaptureSession> => {
    return await apiGet(`/api/v1/runs/${runId}/capture-sessions/${sessionId}`)
  },

  create: async (runId: string, data: CaptureSessionCreate, params?: { project_id?: string }): Promise<CaptureSession> => {
    return await apiPost(`/api/v1/runs/${runId}/capture-sessions`, data, { params })
  },

  stop: async (runId: string, sessionId: string): Promise<CaptureSession> => {
    return await apiPost(`/api/v1/runs/${runId}/capture-sessions/${sessionId}/stop`, {})
  },

  delete: async (runId: string, sessionId: string): Promise<void> => {
    await apiDelete(`/api/v1/runs/${runId}/capture-sessions/${sessionId}`)
  },

  startBackfill: async (runId: string, sessionId: string): Promise<CaptureSession> => {
    return await apiPost(`/api/v1/runs/${runId}/capture-sessions/${sessionId}/backfill/start`)
  },

  completeBackfill: async (
    runId: string,
    sessionId: string
  ): Promise<CaptureSession & { attached_records: number }> => {
    return await apiPost(`/api/v1/runs/${runId}/capture-sessions/${sessionId}/backfill/complete`)
  },
}

// Telemetry Export API
export const telemetryExportApi = {
  /**
   * Export telemetry readings for a single capture session.
   */
  exportSession: async (
    runId: string,
    sessionId: string,
    params?: {
      format?: 'csv' | 'json'
      sensor_id?: string
      signal?: string
      include_late?: boolean
      raw_or_physical?: 'raw' | 'physical' | 'both'
      aggregation?: '1m'
    }
  ): Promise<string> => {
    const response = await apiClient.get(
      `/api/v1/runs/${runId}/capture-sessions/${sessionId}/telemetry/export`,
      {
        params: { ...params, project_id: getActiveProjectId() || undefined },
        responseType: 'text',
      },
    )
    return response.data
  },

  /**
   * Export telemetry readings for all capture sessions of a run.
   */
  exportRun: async (
    runId: string,
    params?: {
      format?: 'csv' | 'json'
      capture_session_id?: string
      sensor_id?: string
      signal?: string
      include_late?: boolean
      raw_or_physical?: 'raw' | 'physical' | 'both'
      aggregation?: '1m'
    }
  ): Promise<string> => {
    const response = await apiClient.get(
      `/api/v1/runs/${runId}/telemetry/export`,
      {
        params: { ...params, project_id: getActiveProjectId() || undefined },
        responseType: 'text',
      },
    )
    return response.data
  },
}

// Telemetry API
export const telemetryApi = {
  ingest: async (data: TelemetryIngest, sensorToken: string): Promise<TelemetryIngestResponse> => {
    // By default, route telemetry through Auth Proxy (same-origin / unified CORS),
    // while preserving sensor token via Authorization header.
    // You can override with direct telemetry-ingest-service URL via VITE_TELEMETRY_INGEST_URL.
    const response = await apiClient.post<TelemetryIngestResponse>(
      '/api/v1/telemetry',
      data,
      {
        baseURL: TELEMETRY_BASE_URL,

        headers: {
          'Authorization': `Bearer ${sensorToken}`,
          'Content-Type': 'application/json',
        },
        _skipAuthInterceptor: true,
      } as AxiosRequestConfig & { _skipAuthInterceptor: boolean }
    )
    return response.data
  },

  stream: async (
    params: {
      sensor_id: string
      since_ts?: string
      since_id?: number
      max_events?: number
      idle_timeout_seconds?: number
    }
  ): Promise<{ response: Response; debug: { url: string; headers: Record<string, string>; method: string } }> => {
    // Stream is user-authenticated via auth-proxy session cookies.
    // Do NOT attach sensor token here (UI doesn't ask for it anymore).
    const url = new URL(`${AUTH_PROXY_URL}/api/v1/telemetry/stream`)
    url.searchParams.set('sensor_id', params.sensor_id)
    const activeProjectId = getActiveProjectId()
    if (activeProjectId) {
      url.searchParams.set('project_id', activeProjectId)
    }
    if (typeof params.since_ts === 'string' && params.since_ts) {
      url.searchParams.set('since_ts', params.since_ts)
    }
    if (typeof params.since_id === 'number') url.searchParams.set('since_id', String(params.since_id))
    if (typeof params.max_events === 'number') url.searchParams.set('max_events', String(params.max_events))
    if (typeof params.idle_timeout_seconds === 'number') {
      url.searchParams.set('idle_timeout_seconds', String(params.idle_timeout_seconds))
    }

    const tryRefresh = async () => {
      const csrf = getCsrfToken()
      const refreshResp = await fetch(`${AUTH_PROXY_URL}/auth/refresh`, {
        method: 'POST',
        headers: {
          ...makeFetchHeaders(),
          'Content-Type': 'application/json',
          ...(csrf ? { 'X-CSRF-Token': csrf } : {}),
        },
        body: JSON.stringify({}),
        credentials: 'include',
      })
      return refreshResp.ok
    }

    const headers = makeFetchHeaders()
    const debug = { url: url.toString(), headers, method: 'GET' }
    try {
      const response = await fetch(debug.url, {
        method: debug.method,
        headers: debug.headers,
        credentials: 'include',
      })
      if (response.status === 401) {
        const refreshed = await tryRefresh()
        if (refreshed) {
          const retryHeaders = makeFetchHeaders()
          const retryDebug = { url: url.toString(), headers: retryHeaders, method: 'GET' }
          const retryResponse = await fetch(retryDebug.url, {
            method: retryDebug.method,
            headers: retryDebug.headers,
            credentials: 'include',
          })
          return { response: retryResponse, debug: retryDebug }
        }
      }
      return { response, debug }
    } catch (e: unknown) {
      // Attach debug info for higher-level error handling / toasts.
      (e as Record<string, unknown>).debug = debug
      throw e
    }
  },

  query: async (params: {
    capture_session_id: string
    sensor_id?: string[]
    since_id?: number
    limit?: number
    include_late?: boolean
    order?: 'asc' | 'desc'
  }): Promise<TelemetryQueryResponse> => {
    const url = new URL(`${TELEMETRY_BASE_URL}/api/v1/telemetry/query`)
    url.searchParams.set('capture_session_id', params.capture_session_id)
    if (Array.isArray(params.sensor_id)) {
      params.sensor_id.forEach((id) => {
        if (id) url.searchParams.append('sensor_id', id)
      })
    }
    if (typeof params.since_id === 'number') url.searchParams.set('since_id', String(params.since_id))
    if (typeof params.limit === 'number') url.searchParams.set('limit', String(params.limit))
    if (typeof params.include_late === 'boolean') {
      url.searchParams.set('include_late', params.include_late ? 'true' : 'false')
    }
    if (params.order === 'asc' || params.order === 'desc') {
      url.searchParams.set('order', params.order)
    }

    return apiFetch<TelemetryQueryResponse>(url.toString())
  },

  aggregated: async (params: {
    capture_session_id: string
    sensor_id?: string[]
    signal?: string
    time_from?: string
    time_to?: string
    limit?: number
    order?: 'asc' | 'desc'
  }): Promise<TelemetryAggregatedResponse> => {
    const url = new URL(`${TELEMETRY_BASE_URL}/api/v1/telemetry/aggregated`)
    url.searchParams.set('capture_session_id', params.capture_session_id)
    if (Array.isArray(params.sensor_id)) {
      params.sensor_id.forEach((id) => {
        if (id) url.searchParams.append('sensor_id', id)
      })
    }
    if (params.signal) url.searchParams.set('signal', params.signal)
    if (params.time_from) url.searchParams.set('time_from', params.time_from)
    if (params.time_to) url.searchParams.set('time_to', params.time_to)
    if (typeof params.limit === 'number') url.searchParams.set('limit', String(params.limit))
    if (params.order === 'asc' || params.order === 'desc') {
      url.searchParams.set('order', params.order)
    }

    return apiFetch<TelemetryAggregatedResponse>(url.toString())
  },
}

// Run Events (Audit Log) API
export const runEventsApi = {
  list: async (runId: string, params?: {
    page?: number
    page_size?: number
  }): Promise<RunEventsListResponse> => {
    return await apiGet(`/api/v1/runs/${runId}/events`, { params })
  },
}

// Capture Session Events (Audit Log) API
export const captureSessionEventsApi = {
  list: async (runId: string, sessionId: string, params?: {
    page?: number
    page_size?: number
  }): Promise<CaptureSessionEventsListResponse> => {
    return await apiGet(`/api/v1/runs/${runId}/capture-sessions/${sessionId}/events`, { params })
  },
}
