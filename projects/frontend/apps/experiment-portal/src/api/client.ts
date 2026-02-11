/** API клиент для взаимодействия с бэкендом через Auth Proxy */
import axios from 'axios'
import type { AxiosRequestConfig } from 'axios'
import type {
  Experiment,
  ExperimentCreate,
  ExperimentUpdate,
  ExperimentsListResponse,
  Run,
  RunCreate,
  RunUpdate,
  RunsListResponse,
  Sensor,
  SensorCreate,
  SensorUpdate,
  SensorsListResponse,
  SensorRegisterResponse,
  SensorTokenResponse,
  CaptureSession,
  CaptureSessionCreate,
  CaptureSessionsListResponse,
  TelemetryIngest,
  TelemetryIngestResponse,
  TelemetryQueryResponse,
  TelemetryAggregatedResponse,
  Project,
  ProjectCreate,
  ProjectUpdate,
  ProjectsListResponse,
  ProjectMember,
  ProjectMemberAdd,
  ProjectMemberUpdate,
  ProjectMembersListResponse,
  RunEventsListResponse,
  CaptureSessionEventsListResponse,
  WebhookSubscription,
  WebhookSubscriptionCreate,
  WebhooksListResponse,
  WebhookDeliveriesListResponse,
} from '../types'
import { generateRequestId } from '../utils/uuid'
import { getTraceId } from '../utils/trace'
import { getActiveProjectId } from '../utils/activeProject'
import { getCsrfToken } from '../utils/csrf'
import {
  buildHttpDebugInfoFromFetch,
  maybeEmitHttpErrorToast,
  maybeEmitHttpErrorToastFromAxiosError,
  truncateString,
} from '../utils/httpDebug'

// API работает через Auth Proxy, который автоматически добавляет токен из куки
const AUTH_PROXY_URL = import.meta.env.VITE_AUTH_PROXY_URL || 'http://localhost:8080'

const apiClient = axios.create({
  baseURL: AUTH_PROXY_URL,
  headers: {
    'Content-Type': 'application/json',
  },
  withCredentials: true, // Важно для работы с HttpOnly куками
  timeout: 30000, // 30 секунд таймаут
})

function _extractProjectIdFromData(data: unknown): string | undefined {
  if (!data) return undefined
  if (typeof data === 'object' && data !== null && !Array.isArray(data)) {
    const projectId = (data as any).project_id
    return typeof projectId === 'string' && projectId ? projectId : undefined
  }
  if (typeof data === 'string') {
    // иногда axios/transform могут давать строку, пробуем распарсить JSON
    try {
      const parsed = JSON.parse(data)
      const projectId = parsed?.project_id
      return typeof projectId === 'string' && projectId ? projectId : undefined
    } catch {
      return undefined
    }
  }
  return undefined
}

function _withProjectIdConfig(
  url: string,
  config: AxiosRequestConfig | undefined,
  data?: unknown
): AxiosRequestConfig | undefined {
  // Проставляем project_id только для experiment-service API (через auth-proxy)
  if (!url.startsWith('/api/')) return config

  const existing = (config?.params as any)?.project_id
  if (existing) return config

  const projectId = _extractProjectIdFromData(data) || getActiveProjectId() || undefined
  if (!projectId) return config

  const next: AxiosRequestConfig = { ...(config || {}) }
  next.params = { ...(next.params as any), project_id: projectId }
  return next
}

async function apiGet<T = any>(url: string, config?: AxiosRequestConfig): Promise<T> {
  const cfg = _withProjectIdConfig(url, config)
  const response = cfg ? await apiClient.get(url, cfg) : await apiClient.get(url)
  return response.data
}

async function apiPost<T = any>(
  url: string,
  data?: any,
  config?: AxiosRequestConfig
): Promise<T> {
  const cfg = _withProjectIdConfig(url, config, data)
  const response = cfg ? await apiClient.post(url, data, cfg) : await apiClient.post(url, data)
  return response.data
}

async function apiPatch<T = any>(
  url: string,
  data?: any,
  config?: AxiosRequestConfig
): Promise<T> {
  const cfg = _withProjectIdConfig(url, config, data)
  const response = cfg ? await apiClient.patch(url, data, cfg) : await apiClient.patch(url, data)
  return response.data
}

async function apiDelete<T = any>(url: string, config?: AxiosRequestConfig): Promise<T> {
  const cfg = _withProjectIdConfig(url, config)
  const response = cfg ? await apiClient.delete(url, cfg) : await apiClient.delete(url)
  return response.data
}

// Interceptor для добавления trace_id и request_id в заголовки
apiClient.interceptors.request.use(
  (config) => {
    // Генерируем request_id для каждого запроса
    const requestId = generateRequestId()
    const traceId = getTraceId()

    // Добавляем заголовки
    config.headers['X-Trace-Id'] = traceId
    config.headers['X-Request-Id'] = requestId

    // CSRF (double-submit cookie) for cookie-authenticated, state-changing requests.
    const method = (config.method || 'get').toUpperCase()
    const isStateChanging = ['POST', 'PUT', 'PATCH', 'DELETE'].includes(method)
    if (isStateChanging) {
      const csrf = getCsrfToken()
      if (csrf) {
        config.headers['X-CSRF-Token'] = csrf
      }
    }

    return config
  },
  (error) => {
    return Promise.reject(error)
  }
)

// Обработка ошибок и автоматическое обновление токена
apiClient.interceptors.response.use(
  (response) => {
    return response
  },
  async (error) => {
    const originalRequest = error.config

    // Если получили 401 и это не повторный запрос
    if (error.response?.status === 401 && !originalRequest._retry) {
      originalRequest._retry = true

      try {
        // Пытаемся обновить токен через Auth Proxy
        const authProxyUrl = import.meta.env.VITE_AUTH_PROXY_URL || 'http://localhost:8080'
        await axios.post(
          `${authProxyUrl}/auth/refresh`,
          {},
          { withCredentials: true }
        )

        // Повторяем оригинальный запрос
        return apiClient(originalRequest)
      } catch (refreshError) {
        // Show debug toast for refresh failure as well (dev-only), then redirect.
        maybeEmitHttpErrorToastFromAxiosError(refreshError)
        // Если refresh не удался - перенаправляем на страницу входа
        window.location.href = '/login'
        return Promise.reject(refreshError)
      }
    }

    // Emit debug toast for any request failure (dev-only; includes network/CORS/timeout).
    maybeEmitHttpErrorToastFromAxiosError(error)

    return Promise.reject(error)
  }
)

// Experiments API
export const experimentsApi = {
  list: async (params?: {
    project_id?: string
    status?: string
    tags?: string
    page?: number
    page_size?: number
  }): Promise<ExperimentsListResponse> => {
    return await apiGet('/api/v1/experiments', { params })
  },

  get: async (id: string): Promise<Experiment> => {
    return await apiGet(`/api/v1/experiments/${id}`)
  },

  create: async (data: ExperimentCreate): Promise<Experiment> => {
    return await apiPost('/api/v1/experiments', data)
  },

  update: async (id: string, data: ExperimentUpdate): Promise<Experiment> => {
    return await apiPatch(`/api/v1/experiments/${id}`, data)
  },

  archive: async (id: string, params?: { project_id?: string }): Promise<Experiment> => {
    return await apiPost(`/api/v1/experiments/${id}/archive`, {}, { params })
  },

  delete: async (id: string): Promise<void> => {
    await apiDelete(`/api/v1/experiments/${id}`)
  },

  search: async (params: {
    q?: string
    project_id?: string
    page?: number
    page_size?: number
  }): Promise<ExperimentsListResponse> => {
    return await apiGet('/api/v1/experiments/search', { params })
  },

  exportData: async (params: {
    project_id?: string
    format?: 'csv' | 'json'
    status?: string
    tags?: string
    created_after?: string
    created_before?: string
  }): Promise<string> => {
    const response = await apiClient.get('/api/v1/experiments/export', {
      params: { ...params, project_id: params.project_id || getActiveProjectId() },
      responseType: 'text',
    })
    return response.data
  },
}

// Runs API
export const runsApi = {
  list: async (
    experimentId: string,
    params?: {
      status?: string
      page?: number
      page_size?: number
    }
  ): Promise<RunsListResponse> => {
    return await apiGet(`/api/v1/experiments/${experimentId}/runs`, {
      params,
    })
  },

  get: async (id: string): Promise<Run> => {
    return await apiGet(`/api/v1/runs/${id}`)
  },

  create: async (experimentId: string, data: RunCreate): Promise<Run> => {
    return await apiPost(`/api/v1/experiments/${experimentId}/runs`, data)
  },

  update: async (id: string, data: RunUpdate): Promise<Run> => {
    return await apiPatch(`/api/v1/runs/${id}`, data)
  },

  complete: async (id: string): Promise<Run> => {
    return await apiPatch(`/api/v1/runs/${id}`, { status: 'succeeded' })
  },

  fail: async (id: string, reason?: string): Promise<Run> => {
    return await apiPatch(`/api/v1/runs/${id}`, { status: 'failed', reason })
  },

  exportData: async (
    experimentId: string,
    params?: {
      format?: 'csv' | 'json'
      status?: string
      tags?: string
      created_after?: string
      created_before?: string
    }
  ): Promise<string> => {
    const response = await apiClient.get(`/api/v1/experiments/${experimentId}/runs/export`, {
      params: { ...params, project_id: getActiveProjectId() || undefined },
      responseType: 'text',
    })
    return response.data
  },

  bulkTags: async (args: {
    run_ids: string[]
    set_tags?: string[]
    add_tags?: string[]
    remove_tags?: string[]
  }): Promise<{ runs: Run[] }> => {
    return await apiPost('/api/v1/runs:bulk-tags', args)
  },
}

// Sensors API
export const sensorsApi = {
  list: async (params?: {
    project_id?: string
    status?: string
    /** Backend pagination (preferred). */
    limit?: number
    offset?: number
    page?: number
    page_size?: number
  }): Promise<SensorsListResponse> => {
    return await apiGet('/api/v1/sensors', { params })
  },

  get: async (id: string, params?: { project_id?: string }): Promise<Sensor> => {
    return await apiGet(`/api/v1/sensors/${id}`, { params })
  },

  create: async (data: SensorCreate): Promise<SensorRegisterResponse> => {
    return await apiPost('/api/v1/sensors', data)
  },

  update: async (id: string, data: SensorUpdate, params?: { project_id?: string }): Promise<Sensor> => {
    return await apiPatch(`/api/v1/sensors/${id}`, data, { params })
  },

  delete: async (id: string, params?: { project_id?: string }): Promise<void> => {
    await apiDelete(`/api/v1/sensors/${id}`, { params })
  },

  rotateToken: async (id: string, params?: { project_id?: string }): Promise<SensorTokenResponse> => {
    return await apiPost(`/api/v1/sensors/${id}/rotate-token`, {}, { params })
  },

  // Multiple projects management
  getProjects: async (id: string): Promise<{ project_ids: string[] }> => {
    // IMPORTANT: use apiGet so project_id is auto-attached (auth-proxy derives X-Project-* from it)
    return await apiGet(`/api/v1/sensors/${id}/projects`)
  },

  addProject: async (id: string, projectId: string): Promise<void> => {
    await apiPost(`/api/v1/sensors/${id}/projects`, { project_id: projectId })
  },

  removeProject: async (id: string, projectId: string): Promise<void> => {
    // Use explicit project_id context for permission checks in that project
    await apiDelete(`/api/v1/sensors/${id}/projects/${projectId}`, { params: { project_id: projectId } })
  },
}

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

// Telemetry API
export const telemetryApi = {
  ingest: async (data: TelemetryIngest, sensorToken: string): Promise<TelemetryIngestResponse> => {
    // By default, route telemetry through Auth Proxy (same-origin / unified CORS),
    // while preserving sensor token via Authorization header.
    // You can override with direct telemetry-ingest-service URL via VITE_TELEMETRY_INGEST_URL.
    const TELEMETRY_BASE_URL =
      import.meta.env.VITE_TELEMETRY_INGEST_URL || AUTH_PROXY_URL
    const response = await apiClient.post<TelemetryIngestResponse>(
      '/api/v1/telemetry',
      data,
      {
        baseURL: TELEMETRY_BASE_URL,
        headers: {
          'Authorization': `Bearer ${sensorToken}`,
          'Content-Type': 'application/json',
        },
      }
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

    const makeHeaders = () => {
      const traceId = getTraceId()
      const requestId = generateRequestId()
      return {
        'X-Trace-Id': traceId,
        'X-Request-Id': requestId,
      } as Record<string, string>
    }

    const tryRefresh = async () => {
      const refreshHeaders = makeHeaders()
      const csrf = getCsrfToken()
      const refreshResp = await fetch(`${AUTH_PROXY_URL}/auth/refresh`, {
        method: 'POST',
        headers: {
          ...refreshHeaders,
          'Content-Type': 'application/json',
          ...(csrf ? { 'X-CSRF-Token': csrf } : {}),
        },
        body: JSON.stringify({}),
        credentials: 'include',
      })
      return refreshResp.ok
    }

    const headers = makeHeaders()
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
          const retryHeaders = makeHeaders()
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
    } catch (e: any) {
      // Attach debug info for higher-level error handling / toasts.
      e.debug = debug
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
    const TELEMETRY_BASE_URL =
      import.meta.env.VITE_TELEMETRY_INGEST_URL || AUTH_PROXY_URL
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

    const headers = {
      'X-Trace-Id': getTraceId(),
      'X-Request-Id': generateRequestId(),
    }

    const resp = await fetch(url.toString(), {
      method: 'GET',
      headers,
      credentials: 'include',
    })

    if (!resp.ok) {
      const text = await resp.text().catch(() => '')
      const bodyText = truncateString(text || '')
      maybeEmitHttpErrorToast(
        buildHttpDebugInfoFromFetch({
          message: text || `Ошибка запроса: HTTP ${resp.status}`,
          request: { method: 'GET', url: url.toString(), headers },
          response: {
            status: resp.status,
            statusText: resp.statusText,
            headers: Object.fromEntries(resp.headers.entries()),
            body: bodyText,
          },
        })
      )
      throw new Error(text || `Ошибка запроса: HTTP ${resp.status}`)
    }

    return (await resp.json()) as TelemetryQueryResponse
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
    const TELEMETRY_BASE_URL =
      import.meta.env.VITE_TELEMETRY_INGEST_URL || AUTH_PROXY_URL
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

    const headers = {
      'X-Trace-Id': getTraceId(),
      'X-Request-Id': generateRequestId(),
    }

    const resp = await fetch(url.toString(), {
      method: 'GET',
      headers,
      credentials: 'include',
    })

    if (!resp.ok) {
      const text = await resp.text().catch(() => '')
      const bodyText = truncateString(text || '')
      maybeEmitHttpErrorToast(
        buildHttpDebugInfoFromFetch({
          message: text || `Ошибка запроса: HTTP ${resp.status}`,
          request: { method: 'GET', url: url.toString(), headers },
          response: {
            status: resp.status,
            statusText: resp.statusText,
            headers: Object.fromEntries(resp.headers.entries()),
            body: bodyText,
          },
        })
      )
      throw new Error(text || `Ошибка запроса: HTTP ${resp.status}`)
    }

    return (await resp.json()) as TelemetryAggregatedResponse
  },
}

// Projects API
export const projectsApi = {
  list: async (): Promise<ProjectsListResponse> => {
    const response = await apiClient.get<ProjectsListResponse>('/projects')
    return response.data
  },

  get: async (id: string): Promise<Project> => {
    const response = await apiClient.get<Project>(`/projects/${id}`)
    return response.data
  },

  create: async (data: ProjectCreate): Promise<Project> => {
    const response = await apiClient.post<Project>('/projects', data)
    return response.data
  },

  update: async (id: string, data: ProjectUpdate): Promise<Project> => {
    const response = await apiClient.put<Project>(`/projects/${id}`, data)
    return response.data
  },

  delete: async (id: string): Promise<void> => {
    await apiClient.delete(`/projects/${id}`)
  },

  listMembers: async (projectId: string): Promise<ProjectMembersListResponse> => {
    const response = await apiClient.get<ProjectMembersListResponse>(
      `/projects/${projectId}/members`
    )
    return response.data
  },

  addMember: async (projectId: string, data: ProjectMemberAdd): Promise<ProjectMember> => {
    const response = await apiClient.post<ProjectMember>(
      `/projects/${projectId}/members`,
      data
    )
    return response.data
  },

  removeMember: async (projectId: string, userId: string): Promise<void> => {
    await apiClient.delete(`/projects/${projectId}/members/${userId}`)
  },

  updateMemberRole: async (
    projectId: string,
    userId: string,
    data: ProjectMemberUpdate
  ): Promise<ProjectMember> => {
    const response = await apiClient.put<ProjectMember>(
      `/projects/${projectId}/members/${userId}/role`,
      data
    )
    return response.data
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

// Webhooks API
export const webhooksApi = {
  list: async (params?: {
    page?: number
    page_size?: number
  }): Promise<WebhooksListResponse> => {
    return await apiGet('/api/v1/webhooks', { params })
  },

  create: async (data: WebhookSubscriptionCreate): Promise<WebhookSubscription> => {
    return await apiPost('/api/v1/webhooks', data)
  },

  delete: async (webhookId: string): Promise<void> => {
    await apiDelete(`/api/v1/webhooks/${webhookId}`)
  },

  listDeliveries: async (params?: {
    status?: string
    page?: number
    page_size?: number
  }): Promise<WebhookDeliveriesListResponse> => {
    return await apiGet('/api/v1/webhooks/deliveries', { params })
  },

  retryDelivery: async (deliveryId: string): Promise<void> => {
    await apiPost(`/api/v1/webhooks/deliveries/${deliveryId}:retry`)
  },
}

